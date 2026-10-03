package main

import (
	"fmt"
	"math"
	"strconv"
	"strings"
)

type View struct {
	ID                   uint64
	Model                string
	Width, Height        int
	Yaw, Pitch, Distance float64
	Profile              string
	Flip                 bool
}

func parseView(body string) (View, error) {
	var view View
	fields := strings.Fields(body)
	if len(body) > 1024 || (len(fields) != 9 && len(fields) != 10) {
		return view, fmt.Errorf("invalid request layout")
	}
	legacy := fields[0] == "NGSREQ2" && len(fields) == 9
	if !legacy && (fields[0] != "NGSREQ3" || len(fields) != 10) {
		return view, fmt.Errorf("unsupported request")
	}
	var err error
	view.ID, err = strconv.ParseUint(fields[1], 10, 64)
	if err != nil || view.ID == 0 {
		return view, fmt.Errorf("invalid identity")
	}
	view.Model = fields[2]
	if len(view.Model) == 0 || len(view.Model) > 64 || strings.IndexFunc(view.Model, func(value rune) bool {
		return !(value >= 'a' && value <= 'z' || value >= 'A' && value <= 'Z' || value >= '0' && value <= '9' || value == '-')
	}) >= 0 {
		return view, fmt.Errorf("invalid model")
	}
	dimensions := []*int{&view.Width, &view.Height}
	for index, target := range dimensions {
		value, failure := strconv.ParseUint(fields[index+3], 10, 16)
		if failure != nil || value < 1 || value > 4096 {
			return view, fmt.Errorf("invalid dimensions")
		}
		*target = int(value)
	}
	values := []*float64{&view.Yaw, &view.Pitch, &view.Distance}
	for index, target := range values {
		value, failure := strconv.ParseFloat(fields[index+5], 64)
		if failure != nil || math.IsNaN(value) || math.IsInf(value, 0) {
			return view, fmt.Errorf("invalid camera")
		}
		*target = value
	}
	if math.Abs(view.Yaw) > 36000 || math.Abs(view.Pitch) > 36000 || legacy && math.Abs(view.Pitch) > 89 || view.Distance < 0.1 || view.Distance > 10 {
		return view, fmt.Errorf("camera outside range")
	}
	view.Profile = fields[8]
	if view.Profile != "rgba" {
		quality, failure := strconv.Atoi(strings.TrimPrefix(view.Profile, "jpeg"))
		if failure != nil || quality < 85 || quality > 95 || view.Profile != fmt.Sprintf("jpeg%d", quality) {
			return view, fmt.Errorf("invalid profile")
		}
	}
	if !legacy {
		if fields[9] != "0" && fields[9] != "1" {
			return view, fmt.Errorf("invalid flip")
		}
		view.Flip = fields[9] == "1"
	}
	return canonical(view), nil
}

func wrap(angle float64) float64 { return angle - 360*math.Floor(angle/360) }
func canonical(view View) View {
	pitch := wrap(view.Pitch+180) - 180
	if pitch > 90 || pitch < -90 {
		if pitch > 90 {
			pitch = 180 - pitch
		} else {
			pitch = -180 - pitch
		}
		view.Yaw += 180
		view.Flip = !view.Flip
	}
	view.Yaw = math.Mod(math.Floor(wrap(view.Yaw)/2+0.5), 180) * 2
	polar := math.Max(0, math.Min(90, math.Floor((90-pitch)/2+0.5)))
	view.Pitch = 90 - polar*2
	distance := math.Max(0, math.Min(80, math.Floor(40+40*math.Log10(view.Distance)+0.5)))
	view.Distance = math.Pow(10, (distance-40)/40)
	return view
}
func (view View) code() string {
	view = canonical(view)
	return fmt.Sprintf("a%03d-t%02d-d%02d", int(view.Yaw/2), int((90-view.Pitch)/2), int(math.Round(40+40*math.Log10(view.Distance))))
}
func (view View) variant() string {
	view = canonical(view)
	flip := 0
	if view.Flip {
		flip = 1
	}
	return fmt.Sprintf("%s-f%d-%dx%d-%s", view.code(), flip, view.Width, view.Height, view.Profile)
}
func (view View) body() string {
	flip := 0
	if view.Flip {
		flip = 1
	}
	return fmt.Sprintf("NGSREQ3 %d %s %d %d %.17g %.17g %.17g %s %d", view.ID, view.Model, view.Width, view.Height, view.Yaw, view.Pitch, view.Distance, view.Profile, flip)
}
