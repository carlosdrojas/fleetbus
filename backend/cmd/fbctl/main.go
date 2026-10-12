// fbctl: command-line client for the FleetBus API.
//
//	fbctl fleet
//	fbctl alerts [-all]
//	fbctl image upload -a app_A.signed -b app_B.signed [-notes "..."]
//	fbctl image list
//	fbctl rollout start -image ID [-canary GW] [-window 30s] [-soak 3s]
//	fbctl rollout status ID [-watch]
//	fbctl rollout abort ID
package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"
)

var base = "http://localhost:8080"

func main() {
	if v := os.Getenv("FLEETBUS_API"); v != "" {
		base = strings.TrimRight(v, "/")
	}
	args := os.Args[1:]
	if len(args) == 0 {
		usage()
	}
	var err error
	switch args[0] {
	case "fleet":
		err = fleet()
	case "alerts":
		err = alerts(args[1:])
	case "image":
		err = image(args[1:])
	case "rollout":
		err = rolloutCmd(args[1:])
	default:
		usage()
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "fbctl:", err)
		os.Exit(1)
	}
}

func usage() {
	fmt.Fprintln(os.Stderr, "usage: fbctl fleet | alerts [-all] | image upload|list | rollout start|status|abort   (API: $FLEETBUS_API, default http://localhost:8080)")
	os.Exit(2)
}

func get(path string, v any) error {
	r, err := http.Get(base + path)
	return decode(r, err, v)
}

func decode(r *http.Response, err error, v any) error {
	if err != nil {
		return err
	}
	defer r.Body.Close()
	b, _ := io.ReadAll(r.Body)
	if r.StatusCode >= 300 {
		var e struct{ Error string }
		if json.Unmarshal(b, &e) == nil && e.Error != "" {
			return fmt.Errorf("%s: %s", r.Status, e.Error)
		}
		return fmt.Errorf("%s: %s", r.Status, strings.TrimSpace(string(b)))
	}
	if v == nil {
		return nil
	}
	return json.Unmarshal(b, v)
}

func fleet() error {
	var f struct {
		Gateways []struct {
			ID        int
			Health    string
			Simulated bool
			AgeMS     int64 `json:"age_ms"`
			Nodes     []struct {
				Name      string
				FwVersion int    `json:"fw_version"`
				SlotName  string `json:"slot_name"`
				StateName string `json:"state_name"`
				Health    string
				Trial     bool
				AgeMS     int64 `json:"age_ms"`
			}
		}
	}
	if err := get("/api/fleet", &f); err != nil {
		return err
	}
	for _, g := range f.Gateways {
		sim := ""
		if g.Simulated {
			sim = " [SIMULATED]"
		}
		fmt.Printf("gateway %d  %s%s  (last seen %dms ago)\n", g.ID, g.Health, sim, g.AgeMS)
		for _, n := range g.Nodes {
			trial := ""
			if n.Trial {
				trial = " TRIAL"
			}
			fmt.Printf("  %-10s v%-3d slot %s%s  %-10s %-8s %dms\n", n.Name, n.FwVersion, n.SlotName, trial, n.StateName, n.Health, n.AgeMS)
		}
	}
	return nil
}

func alerts(args []string) error {
	fs := flag.NewFlagSet("alerts", flag.ExitOnError)
	all := fs.Bool("all", false, "include cleared alerts")
	fs.Parse(args)
	q := ""
	if *all {
		q = "?state=all"
	}
	var rows []struct {
		Rule, Severity, Message string
		RaisedAt                time.Time  `json:"raised_at"`
		ClearedAt               *time.Time `json:"cleared_at"`
	}
	if err := get("/api/alerts"+q, &rows); err != nil {
		return err
	}
	for _, a := range rows {
		state := "OPEN   "
		if a.ClearedAt != nil {
			state = "cleared"
		}
		fmt.Printf("%s %-8s %-16s %s  (%s)\n", state, a.Severity, a.Rule, a.Message, a.RaisedAt.Format(time.TimeOnly))
	}
	if len(rows) == 0 {
		fmt.Println("no alerts")
	}
	return nil
}

func image(args []string) error {
	if len(args) == 0 {
		usage()
	}
	switch args[0] {
	case "list":
		var rows []struct {
			ID      int
			NodeID  int `json:"node_id"`
			Version int
			SizeA   int `json:"size_a"`
			Notes   string
		}
		if err := get("/api/images", &rows); err != nil {
			return err
		}
		for _, i := range rows {
			fmt.Printf("image %d  node %d  v%d  %d B  %s\n", i.ID, i.NodeID, i.Version, i.SizeA, i.Notes)
		}
		return nil
	case "upload":
		fs := flag.NewFlagSet("upload", flag.ExitOnError)
		a := fs.String("a", "", "signed image linked for slot A")
		b := fs.String("b", "", "signed image linked for slot B")
		notes := fs.String("notes", "", "free text")
		fs.Parse(args[1:])
		if *a == "" || *b == "" {
			return fmt.Errorf("need -a and -b")
		}
		var buf bytes.Buffer
		mw := multipart.NewWriter(&buf)
		for field, path := range map[string]string{"slot_a": *a, "slot_b": *b} {
			data, err := os.ReadFile(path)
			if err != nil {
				return err
			}
			fw, _ := mw.CreateFormFile(field, path)
			fw.Write(data)
		}
		mw.WriteField("notes", *notes)
		mw.Close()
		r, err := http.Post(base+"/api/images", mw.FormDataContentType(), &buf)
		var out map[string]any
		if err := decode(r, err, &out); err != nil {
			return err
		}
		fmt.Printf("uploaded: image id %v (node %v, v%v, signature verified by server: %v)\n", out["id"], out["node_id"], out["version"], out["verified"])
		return nil
	}
	usage()
	return nil
}

type rolloutView struct {
	Rollout struct {
		ID            int
		State         string
		Version       int
		FailureReason *string `json:"failure_reason"`
	}
	Steps []struct {
		Ordinal              int
		GatewayID            int `json:"gateway_id"`
		Phase, State, Detail string
		Transfer             *struct {
			NextOffset int `json:"next_offset"`
			Size       int
			State      string
		}
	}
}

func printRollout(v rolloutView) {
	fmt.Printf("rollout %d  v%d  %s", v.Rollout.ID, v.Rollout.Version, strings.ToUpper(v.Rollout.State))
	if v.Rollout.FailureReason != nil {
		fmt.Printf("  (%s)", *v.Rollout.FailureReason)
	}
	fmt.Println()
	for _, s := range v.Steps {
		prog := ""
		if s.Transfer != nil && s.State == "transferring" && s.Transfer.Size > 0 {
			prog = fmt.Sprintf(" %d%%", s.Transfer.NextOffset*100/s.Transfer.Size)
		}
		fmt.Printf("  %d. gw %-3d %-6s %-12s%s  %s\n", s.Ordinal+1, s.GatewayID, s.Phase, s.State, prog, s.Detail)
	}
}

func rolloutCmd(args []string) error {
	if len(args) == 0 {
		usage()
	}
	switch args[0] {
	case "start":
		fs := flag.NewFlagSet("start", flag.ExitOnError)
		img := fs.Int("image", 0, "image id (fbctl image list)")
		canary := fs.Int("canary", 0, "canary gateway id (default: lowest online)")
		window := fs.Duration("window", 30*time.Second, "health window per node")
		soak := fs.Duration("soak", 3*time.Second, "time a node must stay healthy")
		fs.Parse(args[1:])
		body := map[string]any{"image_id": *img, "health_window_ms": window.Milliseconds(), "soak_ms": soak.Milliseconds()}
		if *canary != 0 {
			body["canary_gateway"] = *canary
		}
		b, _ := json.Marshal(body)
		r, err := http.Post(base+"/api/rollouts", "application/json", bytes.NewReader(b))
		var out struct{ ID int }
		if err := decode(r, err, &out); err != nil {
			return err
		}
		fmt.Printf("rollout %d started\n", out.ID)
		return nil
	case "status":
		fs := flag.NewFlagSet("status", flag.ExitOnError)
		watch := fs.Bool("watch", false, "poll until the rollout finishes")
		fs.Parse(args[1:])
		if fs.NArg() != 1 {
			return fmt.Errorf("need a rollout id")
		}
		id, _ := strconv.Atoi(fs.Arg(0))
		for {
			var v rolloutView
			if err := get(fmt.Sprintf("/api/rollouts/%d", id), &v); err != nil {
				return err
			}
			if *watch {
				fmt.Print("\033[2J\033[H")
			}
			printRollout(v)
			if !*watch || (v.Rollout.State != "running" && v.Rollout.State != "pending") {
				if v.Rollout.State == "failed" {
					os.Exit(3)
				}
				return nil
			}
			time.Sleep(500 * time.Millisecond)
		}
	case "abort":
		if len(args) != 2 {
			return fmt.Errorf("need a rollout id")
		}
		r, err := http.Post(base+"/api/rollouts/"+args[1]+"/abort", "application/json", nil)
		if err := decode(r, err, nil); err != nil {
			return err
		}
		fmt.Println("aborted")
		return nil
	}
	usage()
	return nil
}
