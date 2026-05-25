# Homeworlds Text Conventions

This document describes the textual Homeworlds notation used by move formatting, SGF move values, tests, profiling
output, and debug position dumps. The authoritative code is still `homeworlds_move_format()`,
`homeworlds_move_parse()`, and `homeworlds_position_format_ascii()`.

## Pyramids

A pyramid is a color letter followed by a size digit:

- Colors are `r`, `y`, `g`, `b` for red, yellow, green, and blue.
- Sizes are `1`, `2`, `3` for small, medium, and large.
- Stars use uppercase color letters: `R1`, `Y2`, `G3`, `B1`.
- Ships use lowercase color letters: `r1`, `y2`, `g3`, `b1`.

Examples: `G2` is a medium green star, while `g2` is a medium green ship.

## System References

Moves refer to systems in one of two ways:

- `H1` is Player 1's homeworld.
- `H2` is Player 2's homeworld.
- A non-homeworld system is named by its star, using an uppercase pyramid such as `B2`.
- If multiple non-homeworld systems contain the same identifying star, apostrophes disambiguate them in slot order:
  `B2`, `B2'`, `B2''`, and so on.

## Move Text

Setup moves are three pyramids with no separators: the two homeworld stars followed by the starting ship. Stars are
uppercase and the ship is lowercase.

Examples:

- `B1R3g3`
- `R2G1b3`

Turn moves are one or more steps separated by a single space. A turn step starts with a system reference unless it is
`pass`.

Step forms:

- `pass`: pass the turn.
- `<system><color>+`: build the smallest available ship of that color at the system. Example: `H1g+`.
- `<system><ship>=<color>`: trade the named ship for the same-size ship of another color. Example: `H2g2=y`.
- `<system><ship>x<ship>`: attack an enemy ship in the same system. Example: `H1r3xb1`.
- `<system><ship>><system>`: move or discover with the named ship. Example: `H1y3>B2`.
- `<system><ship>-`: sacrifice the named ship. Example: `H1g3-`.
- `<system><color>!`: catastrophe all pyramids of that color at the system. Example: `H1r!`.

Multi-step examples:

- `H1g3- H1g+ H1g+ B2g+`
- `H2g3- B1g+ H2g+ B1g+`

## Position Text

`homeworlds_position_format_ascii()` prints a display and debug representation of the current position. It is not a
general-purpose position parser.

Each non-empty system is one line with three fields:

```text
<Player 2 ships> <stars> <Player 1 ships>
```

Within a field, pyramids are concatenated without separators. A missing field is printed as `-`.

Example:

```text
g3 Y2B3 -

- Y1G3 b3
```

This shows Player 2's homeworld with a `g3` ship at stars `Y2B3`, a blank row separator, and Player 1's homeworld with
a `b3` ship at stars `Y1G3`.

Line order is layout-oriented:

- Player 2's homeworld is printed first when present.
- Non-homeworld systems are grouped into rows according to whether they are connected to Player 2, both homeworlds or
  neither homeworld, or Player 1.
- Player 1's homeworld is printed last when present.
- A blank line is inserted between adjacent printed row groups only when no system in those groups is connected.

An empty position is printed as:

```text
No systems.
```
