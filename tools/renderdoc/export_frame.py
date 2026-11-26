"""Private draw inventory for the RenderDoc GUI Python replay thread."""
import json
from pathlib import Path

def collect_draws(actions, draw_flag):
    stack = list(reversed(list(actions)))
    while stack:
        action = stack.pop()
        if action.flags & draw_flag:
            yield action
        stack.extend(reversed(list(action.children)))

def frame_inventory(controller, rd, event_ids=()):
    draws = list(collect_draws(controller.GetRootActions(), rd.ActionFlags.Drawcall))
    selected = set(event_ids)
    if any(type(i) is not int or i <= 0 for i in selected):
        raise ValueError("event IDs must be positive integers")
    missing = selected.difference(a.eventId for a in draws)
    if missing:
        raise ValueError("not draw events: " + str(sorted(missing)))
    records = []
    for action in draws:
        row = {"event": action.eventId, "indices": action.numIndices,
               "instances": action.numInstances, "flags": int(action.flags)}
        if action.eventId in selected:
            controller.SetFrameEvent(action.eventId, True)
            pipe = controller.GetPipelineState()
            row["pipeline"] = {
                "vs": str(pipe.GetShader(rd.ShaderStage.Vertex)),
                "ps": str(pipe.GetShader(rd.ShaderStage.Pixel)),
                "color": [str(t.resource) for t in pipe.GetOutputTargets()],
                "depth": str(pipe.GetDepthTarget().resource),
            }
        records.append(row)
    return {"schema": "hulk_renderdoc_inventory_v1",
            "draw_count": len(records), "draws": records}

def export_frame(controller, rd, output, event_ids=()):
    path = Path(output).resolve()
    # GUI exec() may inherit __file__ from a different script; also forbid
    # output below any checkout identified by a .git entry.
    if any((parent / ".git").exists() for parent in path.parents):
        raise ValueError("private output must be outside a git checkout")
    if path.suffix.lower() != ".json":
        raise ValueError("output must be a new .json file")
    report = frame_inventory(controller, rd, event_ids)
    with path.open("x", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
        handle.write("\n")
    return report
