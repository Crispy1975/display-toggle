# display-toggle

A macOS command-line tool to toggle any display on or off by UUID.

## The problem

Tools like [displayplacer](https://github.com/jakehilborn/displayplacer) can disable a display using `enabled:false`, but cannot re-enable it. The reason: macOS removes a disabled display's UUID from the online display list, so any tool that looks up displays by UUID finds nothing to act on.

## The solution

`display-toggle` saves a `UUID → CGDirectDisplayID` mapping to disk whenever it sees a display in the online list. When asked to re-enable a display that has since disappeared, it falls back to the saved hardware ID — bypassing the UUID lookup entirely.

It calls the private `CGSConfigureDisplayEnabled` function from the SkyLight framework (the same one `displayplacer` uses internally) via `dlopen`/`dlsym`, since it is not exposed in Apple's public headers.

## Requirements

- macOS (Apple Silicon or Intel)
- Xcode Command Line Tools: `xcode-select --install`

## Install

**Via Homebrew (recommended):**

```sh
brew tap Crispy1975/display-toggle
brew install display-toggle
```

**Build from source:**

```sh
git clone https://github.com/Crispy1975/display-toggle
cd display-toggle
make
make install          # installs to /usr/local/bin
```

Or to a custom prefix:

```sh
make install PREFIX=~/.local
```

## Usage

```sh
# List all online displays with their UUIDs
display-toggle list

# Toggle a display on/off
display-toggle 37D8832A-2D66-02CA-B9F7-8F30A301B230

# Explicitly turn on or off
display-toggle 37D8832A-2D66-02CA-B9F7-8F30A301B230 on
display-toggle 37D8832A-2D66-02CA-B9F7-8F30A301B230 off
```

UUIDs are stable across reboots. Run `display-toggle list` once with all displays connected to register them — this seeds the state file so disabled displays can be re-enabled later.

State is persisted at `~/.config/display-toggle/state`.

## Hammerspoon integration

[Hammerspoon](https://github.com/Hammerspoon/hammerspoon) is a macOS automation tool that lets you bind shell commands to keyboard shortcuts. Bind a key to toggle your MacBook's built-in display (replace the UUID with yours from `display-toggle list`):

```lua
local DISPLAY_TOGGLE = "/usr/local/bin/display-toggle"
local BUILTIN_UUID   = "YOUR-BUILTIN-UUID-HERE"  -- from: display-toggle list

-- Seed state at startup while both displays are active
hs.execute(DISPLAY_TOGGLE .. " list", true)

hs.hotkey.bind({"ctrl", "alt", "cmd"}, "d", function()
    local out = hs.execute(DISPLAY_TOGGLE .. " " .. BUILTIN_UUID, true) or ""
    hs.alert.show("Built-in: " .. (out:match("^(%a+)") or "toggled"))
end)
```

## How it works

1. `display-toggle list` enumerates online displays via `CGGetOnlineDisplayList`, prints their UUIDs, and saves the `UUID → CGDirectDisplayID` mapping to `~/.config/display-toggle/state`.
2. `display-toggle <UUID>` looks up the display in the online list. If found, it calls `CGSConfigureDisplayEnabled` to toggle it and updates the state file.
3. If the display is not in the online list (it was previously disabled), the saved `CGDirectDisplayID` is used to call `CGSConfigureDisplayEnabled` with `true` to re-enable it.

## License

[GNU General Public License v3.0](LICENSE)
