#!/bin/bash
# Claude Code statusLine script
# Captures rate_limits JSON from Claude Code's stdin and caches it locally.
# Configure in ~/.claude/settings.json:
#   "statusLine": { "type": "command", "command": "/path/to/status-line.sh" }

CACHE_FILE="/tmp/cc-display-claude.json"

input=$(cat)

# Extract rate_limits from the JSON that Claude Code pipes in
_rl=$(echo "$input" | jq -c '.rate_limits // empty' 2>/dev/null)

if [ -n "$_rl" ]; then
    printf '%s\n' "$_rl" > "$CACHE_FILE"
fi

# Output status line text (displayed in Claude Code's UI)
echo "$input" | jq -r '"[\(.model.display_name // "Claude")] \(.context_window.used_percentage // 0)% ctx"' 2>/dev/null
