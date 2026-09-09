# Fish completions for coreliquidctl.
# Install to ~/.config/fish/completions/ or /usr/share/fish/vendor_completions.d/

# Nothing here takes a file name
complete -c coreliquidctl -f

# Subcommands, long form
complete -c coreliquidctl -n __fish_use_subcommand -a status  -d 'Current mode and CPU temperature (default)'
complete -c coreliquidctl -n __fish_use_subcommand -a silent  -d 'Silent mode (0)'
complete -c coreliquidctl -n __fish_use_subcommand -a balance -d 'Balance mode (1)'
complete -c coreliquidctl -n __fish_use_subcommand -a game    -d 'Game mode (2)'
complete -c coreliquidctl -n __fish_use_subcommand -a default -d 'Default, constant mode (4)'
complete -c coreliquidctl -n __fish_use_subcommand -a smart   -d 'Smart mode (5)'
complete -c coreliquidctl -n __fish_use_subcommand -a mode    -d 'Switch to a mode by number'
complete -c coreliquidctl -n __fish_use_subcommand -a ping    -d 'Check that the daemon answers'
complete -c coreliquidctl -n __fish_use_subcommand -a raw     -d 'Send one protocol line as is'
complete -c coreliquidctl -n __fish_use_subcommand -a help    -d 'Show usage'

# Short aliases. Spelled out so that 's' does not silently look like 'smart'.
complete -c coreliquidctl -n __fish_use_subcommand -a st -d 'status: current mode and CPU temperature'
complete -c coreliquidctl -n __fish_use_subcommand -a s  -d 'silent (0) -- not smart, not status'
complete -c coreliquidctl -n __fish_use_subcommand -a b  -d 'balance (1)'
complete -c coreliquidctl -n __fish_use_subcommand -a g  -d 'game (2)'
complete -c coreliquidctl -n __fish_use_subcommand -a d  -d 'default (4)'
complete -c coreliquidctl -n __fish_use_subcommand -a m  -d 'smart (5)'
complete -c coreliquidctl -n __fish_use_subcommand -a sm -d 'smart (5)'
complete -c coreliquidctl -n __fish_use_subcommand -a p  -d 'ping: check that the daemon answers'

complete -c coreliquidctl -s h -l help -d 'Show usage'

# Mode numbers, for 'coreliquidctl mode <n>'. 3 is customize, which the daemon
# rejects, so it is not offered.
complete -c coreliquidctl -n '__fish_seen_subcommand_from mode' -a 0 -d silent
complete -c coreliquidctl -n '__fish_seen_subcommand_from mode' -a 1 -d balance
complete -c coreliquidctl -n '__fish_seen_subcommand_from mode' -a 2 -d game
complete -c coreliquidctl -n '__fish_seen_subcommand_from mode' -a 4 -d 'default (constant)'
complete -c coreliquidctl -n '__fish_seen_subcommand_from mode' -a 5 -d smart

# Protocol lines, for 'coreliquidctl raw <line>'
complete -c coreliquidctl -n '__fish_seen_subcommand_from raw' -a STATUS -d 'OK mode=<n> temp=<c>'
complete -c coreliquidctl -n '__fish_seen_subcommand_from raw' -a PING   -d OK
complete -c coreliquidctl -n '__fish_seen_subcommand_from raw' -a MODE   -d 'MODE <n>'
complete -c coreliquidctl -n '__fish_seen_subcommand_from raw' -a CAPS   -d 'Protocol version 2 and later'
