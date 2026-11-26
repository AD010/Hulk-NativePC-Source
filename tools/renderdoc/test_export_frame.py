import unittest
from types import SimpleNamespace as NS
from export_frame import frame_inventory, export_frame
from tempfile import TemporaryDirectory
from pathlib import Path

def action(i, flags, children=()):
    return NS(eventId=i, flags=flags, children=children, numIndices=6, numInstances=1)

class FakeReplay:
    def __init__(self):
        self.events = []
    def GetRootActions(self):
        return [action(1, 0, [action(2, 1), action(3, 0, [action(4, 1)])])]
    def SetFrameEvent(self, event, force):
        self.events.append(event)
    def GetPipelineState(self):
        return NS(GetShader=lambda stage: stage,
                  GetOutputTargets=lambda: [NS(resource=9)],
                  GetDepthTarget=lambda: NS(resource=10))

rd = NS(ActionFlags=NS(Drawcall=1), ShaderStage=NS(Vertex=1, Pixel=2))

class Tests(unittest.TestCase):
    def test_nested_draws_and_selected_pipeline(self):
        replay = FakeReplay()
        report = frame_inventory(replay, rd, [4])
        self.assertEqual(report["draw_count"], 2)
        self.assertEqual([x["event"] for x in report["draws"]], [2,4])
        self.assertEqual(replay.events, [4])
        self.assertNotIn("pipeline", report["draws"][0])
        self.assertEqual(report["draws"][1]["pipeline"]["color"], ["9"])
    def test_invalid_selection(self):
        replay = FakeReplay()
        with self.assertRaises(ValueError):
            frame_inventory(replay, rd, [3])
        self.assertEqual(replay.events, [])
    def test_private_output_and_no_overwrite(self):
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "a.json"
            export_frame(FakeReplay(), rd, path)
            with self.assertRaises(FileExistsError):
                export_frame(FakeReplay(), rd, path)
            (Path(tmp) / ".git").mkdir()
            with self.assertRaises(ValueError):
                export_frame(FakeReplay(), rd, Path(tmp) / "b.json")

if __name__ == "__main__":
    unittest.main()
