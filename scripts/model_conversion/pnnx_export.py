import sys
import torch
import pnnx
from pathlib import Path

weights = Path(sys.argv[1])
out_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else weights.parent
stem    = sys.argv[3] if len(sys.argv) > 3 else weights.stem

yolov9_src = Path("/home/oliver/uni_projects/ROS2/Vermin_Collector_ROS2_3D_Object_Detection/object_detection/object_detection")
sys.path.insert(0, str(yolov9_src))

print(f"Loading {weights} ...")
ckpt  = torch.load(str(weights), map_location='cpu', weights_only=False)
model = (ckpt.get('ema') or ckpt['model']).float().eval()

for m in model.modules():
    if type(m).__name__ in ('DualDDetect', 'Detect', 'DDetect'):
        m.export = True

dummy = torch.rand(1, 3, 352, 640)  # HxW: matches Python inference (640w x 352h)
with torch.no_grad():
    model(dummy)  # warm-up

pt_path  = out_dir / (stem + '.pt')
out_base = out_dir / stem
out_dir.mkdir(parents=True, exist_ok=True)

print(f"Calling pnnx.export -> {out_base}.ncnn.{{param,bin}} ...")
import warnings
with warnings.catch_warnings():
    warnings.filterwarnings('ignore', category=torch.jit.TracerWarning)
    pnnx.export(
        model,
        str(pt_path),
        inputs=(dummy,),
        ncnnparam=str(out_base) + '.ncnn.param',
        ncnnbin=str(out_base) + '.ncnn.bin',
        fp16=True,
        optlevel=2,
        check_trace=False,
    )

pt_path.unlink(missing_ok=True)
print("Done.")
