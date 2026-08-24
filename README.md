# MT4 AutoTrading Emergency Manager

## Requested sequence

```text
AutoTrading ON?
       |
      YES
       v
Turn AutoTrading OFF
       |
       v
Find open positions
       |
       v
Close position #1
       |
       v
WAIT 5 seconds
       |
       v
Close position #2
       |
       v
WAIT 5 seconds
       |
       v
Close position #3
       |
      ...
       |
       v
Verify ZERO positions
```

## Important technical limitation

The project intentionally separates the two operations:

1. **AutoTrading control** — the Windows program can attempt to turn the
   MT4 terminal's AutoTrading switch OFF using the terminal window command.

2. **Trade execution** — a normal external EXE cannot universally enumerate and
   close an MT4 retail account without an authorized external trading API.
   MT4's MQL4 `OrderClose()` also cannot be used by a normal EA when global
   AutoTrading/trading permission is disabled.

For that reason the included `UnconfiguredTradeAdapter` refuses to claim that
positions were closed. It is a compile-ready adapter boundary, not a fake
broker API.

## To make real closing work

Replace `UnconfiguredTradeAdapter` with an implementation of `ITradeAdapter`
for an authorized trading interface available from your broker/provider.

The adapter must implement:

- `GetOpenPositions()`
- `ClosePosition()`

The manager will then enforce the requested 5-second interval between
successful close requests and will refresh the position list after every
close.

## Build online

Push the files to GitHub, then:

1. Open **Actions**.
2. Select **Build MT4 AutoTrading Emergency Manager**.
3. Click **Run workflow**.
4. Download the artifact:
   `MT4_AutoTrading_Emergency_Manager-windows-x64`

## Safety

Test with a demo account first.

The AutoTrading switch is terminal-wide and affects every EA in that MT4
terminal.

Do not use the program on a live account until the AutoTrading UI detection
and the external trade adapter have been tested thoroughly.
