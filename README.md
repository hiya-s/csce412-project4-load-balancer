# CSCE 412 Project 4 — Load Balancer

A C++17 discrete-time simulation of a switch routing requests to two
independent, dynamically scaled load balancers. Processing (`P`) and streaming
(`S`) each have their own FIFO queue, servers, scaling cooldown, and statistics;
the two balancers advance together on one simulation clock.

## Build

From this directory, compile with:

```sh
make
```

The Makefile builds `load_balancer`; `make clean` removes generated object
files and the executable. With a C++17 compiler installed, a direct build is:

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 main.cpp Config.cpp IPBlocker.cpp WebServer.cpp LoadBalancer.cpp Switch.cpp -o load_balancer
```

## Run

Run interactively to enter the starting server count and duration (press Enter
to use the configured defaults):

```sh
./load_balancer
```

Use flags for a non-interactive run. To run with 20 total starting servers for 100,000 cycles in quiet mode:

```sh
./load_balancer --servers 20 --cycles 100000 --quiet --log project4-summary.log
```

To create a useful event log instead, run the same workload in verbose mode:

```sh
./load_balancer --servers 20 --cycles 100000 --verbose --log project4-verbose.log
```

`--quiet` prints only the starting and ending summaries. `--verbose` also
prints and logs request completions, blocked requests, server scaling actions,
and periodic progress snapshots; it does not print every clock cycle or
routine accepted/dispatched requests. Snapshots are controlled by
`progress_snapshot_interval` (set it to `0` to disable them). Both modes write
the starting and ending summaries to the selected log file, and verbose mode
adds the event details. Console event colors are not written to the log.
Use `--help` to show all command-line options. For Windows PowerShell, invoke
the executable as `.\load_balancer.exe` with the same options.

[`project4-summary.log`](project4-summary.log) records the checked quiet-mode
run, and [`project4-verbose.log`](project4-verbose.log) records the full
verbose-mode run. Both use 20 as the **total starting server count**, split
evenly between the P and S balancers, and run for 100,000 cycles. Each report
includes both per-balancer statistics and aggregate request accounting.
Automatic scaling can change the total server count during the run. The
quiet and verbose runs use the same seed and configuration, so their
simulation totals match.

| Result | Total | Processing balancer | Streaming balancer |
| --- | ---: | ---: | ---: |
| Servers at start | 20 | 10 | 10 |
| Servers at end | 4 | 2 | 2 |
| Initial accepted queue | 1,995 | 1,004 | 991 |
| Generated requests | 66,949 | 33,369 | 33,580 |
| Accepted requests | 66,725 | 33,238 | 33,487 |
| Blocked requests | 224 | 131 | 93 |
| Completed requests | 66,614 | 33,232 | 33,382 |
| Remaining queued | 107 | 4 | 103 |
| Active requests at end | 4 | 2 | 2 |
| Servers added | 27 | 15 | 12 |
| Servers removed | 43 | 23 | 20 |
| Cycles completed | 100,000 | — | — |
| Request accounting | PASS | — | — |

The aggregate accounting relationship is
`generated = blocked + completed + queued + still processing`:
`66,949 = 224 + 66,614 + 107 + 4`.

## Configuration

Settings use `key = value` syntax. Lines beginning with `#` and trailing
comments are ignored. A file path may be supplied as `--config FILE`.

| Setting | Meaning |
| --- | --- |
| `simulation_cycles` | Default run duration in clock cycles |
| `initial_servers` | Default starting server count; can be overridden with `--servers` |
| `min_servers`, `max_servers` | Minimum and maximum total server count shared across the P/S balancers |
| `initial_requests_per_server` | Initial queue fill per starting server (default 100) |
| `min_processing_time`, `max_processing_time` | Inclusive request service-time bounds |
| `arrival_probability` | Probability of an arrival batch each cycle, from 0 to 1 |
| `arrival_batch_size` | Number of requests generated when an arrival occurs |
| `progress_snapshot_interval` | Verbose progress snapshot interval in cycles; zero disables snapshots |
| `scale_up_queue_per_server` | Add one server when the waiting queue is above this multiple of server count (default 80) |
| `scale_down_queue_per_server` | Remove one idle server when the waiting queue is below this multiple (default 50) |
| `scaling_cooldown_cycles` | Wait this many cycles between successful scale operations |
| `random_seed` | Fixed random seed; zero chooses a non-fixed seed |
| `verbose` | Default console event detail (`true` or `false`) |
| `log_file` | Default path for the run log |
| `blocked_ips` | Comma-separated IPv4 addresses and CIDR ranges |

The `Switch` creates two independent `LoadBalancer` objects, one per job type.
It applies the IPv4 blocker and routes each accepted request to the matching
balancer. The user-provided starting server count is split as evenly as
possible, with an odd extra server assigned to processing; at least two
starting servers are required. Each balancer has a private FIFO queue, server
pool, scaling cooldown, and counters. Both balancers process each shared clock
cycle, and their individual plus combined statistics appear in the summaries.
Each keeps at least one server and removes only idle servers.

## Method and cycle design

The [design page](design.html) records method inputs and return values. Each
cycle follows this order: generate arrivals; firewall-check and route them;
check each balancer's independent scaling cooldown and queue threshold; tick
servers that were already busy; record completions; dispatch each FIFO to idle
servers in its own balancer; then advance the shared clock. A request assigned
during a cycle starts processing on the next cycle. `WebServer::tick()` returns
true on the exact cycle a request completes. Servers are owned by
`std::unique_ptr` and idle server objects are destroyed when scaled down.

Run focused regression tests with:

```sh
make test
```

They cover five-cycle processing, FIFO dispatch to the correct independent
balancer, per-balancer scale-up/cooldown, scale-down safety/minimum capacity,
CIDR boundaries, blocked initial and later requests, accounting, and invalid
configuration.

## Documentation and design

[`design.html`](design.html) is the workflow and design page. The
[`Doxyfile`](Doxyfile) generates HTML API documentation under `docs/api`, using
this README as the landing page and including the design page and configuration
file in the output.

Generate or refresh the documentation after changing source comments:

```sh
doxygen Doxyfile
```

Open `docs/api/index.html` to review the generated API documentation locally.
The ready-to-upload [`project4-doxygen-html.zip`](project4-doxygen-html.zip)
contains the generated HTML site, design page, config, Doxygen settings, and
simulation log. Extract it and upload its contents to the documentation folder
on `people.tamu.edu`. The generated HTML is separate from the source ZIP.
