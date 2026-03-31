# standard imports
from math import cos, pi, sin, sqrt
from pathlib import Path

# variables
SIZE = 512
BACKGROUND = (0x2A, 0x2D, 0x30)
LOGO_GRAY = (0x56, 0x5C, 0x64)
LOGO_WHITE = (0xFF, 0xFF, 0xFF)
CENTER = (SIZE - 1) / 2.0
OUTER_RADIUS = CENTER
INNER_RADIUS = (95.5 / 128.0) * OUTER_RADIUS
SPOKE_HALF_WIDTH = 8.0
SUPERSAMPLE_OFFSETS = ((0.25, 0.25), (0.75, 0.25), (0.25, 0.75), (0.75, 0.75))
SPOKE_DIRECTIONS = tuple((cos(index * pi / 4.0), sin(index * pi / 4.0)) for index in range(8))


def sample_logo(x: float, y: float) -> tuple[int, int, int]:
    dx = x - CENTER
    dy = y - CENTER
    distance = sqrt(dx * dx + dy * dy)

    if distance > OUTER_RADIUS:
        return BACKGROUND

    color = LOGO_GRAY
    if distance <= INNER_RADIUS:
        color = LOGO_WHITE
        for direction_x, direction_y in SPOKE_DIRECTIONS:
            projection = dx * direction_x + dy * direction_y
            if projection < 0.0 or projection > INNER_RADIUS:
                continue

            perpendicular = abs(dx * direction_y - dy * direction_x)
            if perpendicular <= SPOKE_HALF_WIDTH:
                return LOGO_GRAY

    return color


def render_logo() -> bytes:
    pixels = bytearray()
    sample_count = len(SUPERSAMPLE_OFFSETS)
    for y in range(SIZE):
        for x in range(SIZE):
            red = 0
            green = 0
            blue = 0
            for offset_x, offset_y in SUPERSAMPLE_OFFSETS:
                sample_red, sample_green, sample_blue = sample_logo(x + offset_x, y + offset_y)
                red += sample_red
                green += sample_green
                blue += sample_blue

            pixels.extend((red // sample_count, green // sample_count, blue // sample_count))

    return bytes(pixels)


def main() -> None:
    output_path = Path(__file__).resolve().parents[1] / "xbe" / "assets" / "moonlight-logo.ppm"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as output_file:
        output_file.write(f"P6\n{SIZE} {SIZE}\n255\n".encode("ascii"))
        output_file.write(render_logo())
        output_file.write(b"\n")


if __name__ == "__main__":
    main()
