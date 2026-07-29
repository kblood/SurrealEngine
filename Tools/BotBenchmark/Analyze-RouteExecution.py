#!/usr/bin/env python3
"""Fail-closed active-MoveToward stall analysis for route-execution telemetry."""
from __future__ import annotations
import argparse, json, math, sys
from collections import defaultdict
from pathlib import Path
from typing import Any

SCHEMA = "surreal-bot-route-execution-analysis-v1"

class RouteError(ValueError): pass

def load_lines(path: Path) -> list[dict[str, Any]]:
    try: rows=[json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line]
    except (OSError,json.JSONDecodeError) as exc: raise RouteError(f"cannot read {path}: {exc}") from exc
    if not rows: raise RouteError(f"{path}: no records")
    return rows

def analyze(run: Path, *, minimum_ticks: int=120, progress_radius: float=4.0) -> dict[str, Any]:
    if minimum_ticks < 1 or not math.isfinite(progress_radius) or progress_radius < 0: raise RouteError("invalid thresholds")
    route=load_lines(run/"route-execution.jsonl"); events=load_lines(run/"events.jsonl")
    event_by_tick={int(row["tick"]):row for row in events if row.get("type")=="tick"}
    if len(event_by_tick)!=len(route): raise RouteError("route records do not reconcile with tick telemetry")
    traces=defaultdict(list)
    for record in route:
        if record.get("schema")!="surreal-bot-route-execution-observation-v1": raise RouteError("unexpected route schema")
        tick=int(record["tick"]); event=event_by_tick.get(tick)
        if event is None: raise RouteError(f"missing tick telemetry for route tick {tick}")
        telemetry={b["identity"]:b for b in event.get("bots",[])}
        for b in record.get("participants",[]):
            identity=b.get("identity"); live=telemetry.get(identity)
            if live is None: raise RouteError(f"route participant {identity!r} absent from tick telemetry")
            active=(b.get("available") is True and live.get("latent_action")=="MoveToward" and bool(b.get("move_target")))
            traces[identity].append((tick,active,b,live))
    episodes=[]
    native_detection_events=[]
    for identity, samples in sorted(traces.items()):
        start=None
        previous_detections=0
        for tick, active, route_state, live in samples:
            raw_detections=live.get("move_stall_detections_exact", "0")
            try: detections=int(raw_detections)
            except (TypeError,ValueError): raise RouteError(f"invalid native detection counter for {identity!r}")
            if detections < previous_detections: raise RouteError(f"native detection counter regressed for {identity!r}")
            if detections > previous_detections:
                native_detection_events.append({"identity":identity,"tick":tick,"detections_exact":detections,"move_target":route_state.get("move_target"),"route_cache":route_state.get("route_cache",[]),"latent_action":live.get("latent_action"),"forced_replans_exact":live.get("move_stall_forced_replans_exact")})
            previous_detections=detections
            stagnant=active and route_state.get("progress_known") is True and float(route_state.get("displacement_since_previous_tick",0.0)) < progress_radius
            if stagnant and start is None: start=(tick,route_state,live)
            if not stagnant and start is not None:
                if tick-start[0]>=minimum_ticks:
                    episodes.append({"identity":identity,"start_tick":start[0],"end_tick":tick-1,"duration_ticks":tick-start[0],"move_target":start[1].get("move_target"),"route_cache":start[1].get("route_cache",[]),"watchdog_detections_at_start":start[2].get("move_stall_detections_exact")})
                start=None
    return {"schema":SCHEMA,"minimum_ticks":minimum_ticks,"progress_radius":progress_radius,"episodes":episodes,"native_detection_events":native_detection_events,"selection_safe":False}

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("run",type=Path);p.add_argument("--minimum-ticks",type=int,default=120);a=p.parse_args(argv)
    try: print(json.dumps(analyze(a.run,minimum_ticks=a.minimum_ticks),indent=2,sort_keys=True))
    except RouteError as exc: print(f"error: {exc}",file=sys.stderr);return 2
    return 0
if __name__=="__main__": raise SystemExit(main())
