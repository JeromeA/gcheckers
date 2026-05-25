# Homeworlds Text Conventions

This document describes the textual Homeworlds notation that move formatting, SGF move values, tests, profiling
output, and debug position dumps should use. The implementation entry points are `homeworlds_move_format()`,
`homeworlds_move_parse()`, and `homeworlds_position_format_ascii()`.

## Pyramids

A pyramid is a color letter followed by a size digit:

- Colors are `r`, `y`, `g`, `b` for red, yellow, green, and blue.
- Sizes are `1`, `2`, `3` for small, medium, and large.
- Stars use uppercase color letters: `R1`, `Y2`, `G3`, `B1`.
- Ships use lowercase color letters: `r1`, `y2`, `g3`, `b1`.

Examples: `G2` is a medium green star, while `g2` is a medium green ship.

## System References

Moves and position text refer to systems by stable system labels:

- `H1` is Player 1's homeworld.
- `H2` is Player 2's homeworld.
- `S0`, `S1`, `S2`, and so on are non-homeworld systems in internal slot order, ignoring the two homeworld slots.

For example, `S0` is the first non-homeworld system slot, `S1` is the second, and the labels do not depend on the
system's star color or size. Two systems with the same star are still distinct because they have different `S` labels.
There are no apostrophe suffixes.

## Move Text

Setup moves are three pyramids with no separators: the two homeworld stars followed by the starting ship. Stars are
uppercase and the ship is lowercase.

Examples:

- `B1R3g3`
- `R2G1b3`

Turn moves are one or more steps separated by a single space. The parser accepts spaces only between complete steps.
A turn step starts with a system reference unless it is `pass`.

Step forms:

- `pass`: pass the turn.
- `<system><color>+`: build the smallest available ship of that color at the system. Example: `H1g+`.
- `<system><ship>=<color>`: trade the named ship for the same-size ship of another color. Example: `S1g2=r`.
- `<system><ship>x<ship>`: attack an enemy ship in the same system. Example: `S1r3xb1`.
- `<system><ship>><system>`: move the named ship to an existing system. Example: `S1g2>S2`.
- `<system><ship>><system>(<star>)`: discover a new system with the named ship. Example: `S1g2>S2(Y2)`.
- `<system><ship>-`: sacrifice the named ship. Example: `H1g3-`.
- `<system><color>!`: catastrophe all pyramids of that color at the system. Example: `S1r!`.

Multi-step examples:

- `H1g3- S0g+ H2g+ S0g+`
- `S1g3- H1g+ H1g+ S0g+ S0g!`

## Position Text

`homeworlds_position_format_ascii()` prints a display and debug representation of the current position. Position text
uses the same system labels as move text.

Each non-empty system is one line with a system label and three fields:

```text
<system>: <Player 2 ships> <stars> <Player 1 ships>
```

Within a field, pyramids are concatenated without separators. A missing field is printed as `-`.

Example:

```text
H2: g3 Y2B3 -
S0: - B2 g1

H1: - Y1G3 b3
```

This shows Player 2's homeworld with a `g3` ship at stars `Y2B3`, non-homeworld system `S0` with a `B2` star and a
Player 1 `g1` ship, and Player 1's homeworld with a `b3` ship at stars `Y1G3`.

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
