package main

import (
	"math"
	"math/rand"
	"time"
)

func hardClip(v int32, threshold int32) int16 {
	if v > threshold {
		return int16(threshold)
	}
	if v < -threshold {
		return int16(-threshold)
	}
	return int16(v)
}

func hardClipFloat(v float64, threshold float64) int16 {
	if v > threshold {
		return int16(threshold)
	}
	if v < -threshold {
		return int16(-threshold)
	}
	return int16(v)
}

func dbToLinear(db float64) float64 {
	return math.Pow(10.0, db/20.0)
}

func randomDurationMs(minMs int, maxMs int) time.Duration {
	if maxMs <= minMs {
		return time.Duration(minMs) * time.Millisecond
	}
	return time.Duration(minMs+rand.Intn(maxMs-minMs+1)) * time.Millisecond
}

func randomDurationSec(minSec int, maxSec int) time.Duration {
	if maxSec <= minSec {
		return time.Duration(minSec) * time.Second
	}
	return time.Duration(minSec+rand.Intn(maxSec-minSec+1)) * time.Second
}

func randomFloat64(min float64, max float64) float64 {
	if max <= min {
		return min
	}
	return min + rand.Float64()*(max-min)
}

func maxInt(a int, b int) int {
	if a > b {
		return a
	}
	return b
}

func boolToInt(v bool) int {
	if v {
		return 1
	}
	return 0
}
