from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FW_MAIN = ROOT / "firmware" / "esp32c3" / "main"


def main() -> None:
    cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    assert cc is not None, "C compiler is required for the eye render check"

    source = f"""
#include <assert.h>
#include <stdint.h>

#include "{FW_MAIN / "eyes.c"}"
#include "{FW_MAIN / "screen.c"}"

static void assert_rects_inside_screen(xob_screen_frame_t frame) {{
    for (uint8_t i = 0; i < frame.count; i++) {{
        xob_screen_rect_t rect = frame.rects[i];
        assert(rect.x >= 0);
        assert(rect.y >= 0);
        assert(rect.w > 0);
        assert(rect.h > 0);
        assert(rect.x + rect.w <= XOB_SCREEN_WIDTH);
        assert(rect.y + rect.h <= XOB_SCREEN_HEIGHT);
    }}
}}

int main(void) {{
    xob_screen_frame_t empty = xob_screen_render_eyes(0);
    assert(empty.count == 0);

    xob_eyes_frame_t idle = xob_eyes_frame(XOB_EYES_IDLE, 0);
    xob_screen_frame_t open = xob_screen_render_eyes(&idle);
    assert(open.count == 5);
    assert(open.rects[0].x == 0);
    assert(open.rects[0].y == 0);
    assert(open.rects[0].w == XOB_SCREEN_WIDTH);
    assert(open.rects[0].h == XOB_SCREEN_HEIGHT);
    assert_rects_inside_screen(open);

    xob_eyes_frame_t blink = xob_eyes_frame(XOB_EYES_IDLE, 80);
    xob_screen_frame_t closed = xob_screen_render_eyes(&blink);
    assert(closed.count == 3);
    assert_rects_inside_screen(closed);

    xob_eyes_frame_t thinking = xob_eyes_frame(XOB_EYES_THINKING, 500);
    xob_screen_frame_t shifted = xob_screen_render_eyes(&thinking);
    assert(shifted.count == 5);
    assert_rects_inside_screen(shifted);

    return 0;
}}
"""

    with tempfile.TemporaryDirectory() as tmp:
        source_path = Path(tmp) / "check_eye_render.c"
        binary_path = Path(tmp) / "check_eye_render"
        source_path.write_text(source)
        subprocess.run(
            [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(FW_MAIN), str(source_path), "-o", str(binary_path)],
            check=True,
        )
        subprocess.run([str(binary_path)], check=True)

    print("check_eye_render ok")


if __name__ == "__main__":
    main()
