# CSCE 412 Project 4 — Load Balancer

A C++17 discrete-time simulation of job-type routing, FIFO request queues, an
IP range blocker, and dynamically scaled web-server pools. Processing (`P`)
and streaming (`S`) requests each have a dedicated queue and server pool.

## Build

From this directory, compile with:

```sh
make
```

The Makefile builds `load_balancer`; `make clean` removes generated object
files and the executable. With a C++17 compiler installed, a direct build is:

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 main.cpp Config.cpp IPBlocker.cpp WebServer.cpp LoadBalancer.cpp -o load_balancer
```

## Run

Run interactively to enter the starting server count and duration (press Enter
to use the configured defaults):

```sh
./load_balancer
```

Use flags for a non-interactive run. To reproduce the required 20-starting-
server, 100,000-cycle quiet run and write its summary to a log:

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

### Required 20-server, 100,000-cycle run

[`project4-summary.log`](project4-summary.log) is the actual quiet-mode output
from the required run. It used the settings shown in `config.txt`, with the
server count and duration overridden by the command above. The run started
with 20 servers (10 processing and 10 streaming), generated 2,000 initial
requests, and blocked 5 of those, leaving an initial queue of 1,995. Because
scaling is enabled, 20 is the starting count, not a fixed count.

| Result | Value |
| --- | ---: |
| Requested and completed clock cycles | 100,000 |
| Initial servers (processing / streaming) | 20 (10 / 10) |
| Final servers (processing / streaming) | 4 (2 / 2) |
| Initial queue after firewall checks | 1,995 |
| Generated requests | 66,949 |
| Blocked requests | 224 |
| Completed requests (processing / streaming) | 66,614 (33,159 / 33,455) |
| Requests remaining in queues | 107 (77 processing / 30 streaming) |
| Requests still being processed | 4 |
| Servers added / removed | 27 / 43 |
| Request accounting | PASS |

The accounting check is
`generated = blocked + completed + queued + still processing`; for this run,
`66,949 = 224 + 66,614 + 107 + 4`. Generated totals include initial and
later-arriving requests. The initial queue count reflects only requests
accepted by the firewall.

## Configuration

Settings use `key = value` syntax. Lines beginning with `#` and trailing
comments are ignored. A file path may be supplied as `--config FILE`.

| Setting | Meaning |
| --- | --- |
| `simulation_cycles` | Default run duration in clock cycles |
| `initial_servers` | Default starting server count; can be overridden with `--servers` |
| `min_servers`, `max_servers` | Minimum and maximum total server count across both pools |
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

The user-provided starting server count is the total across both pools; it is
split as evenly as possible, with an odd extra server assigned to processing.
At least two starting servers are required so each job type has a server.
Each pool scales against its own waiting queue and server count. A pool always
keeps one server; only idle servers are removed. Accepted requests are checked
against blocked ranges before entering their type-specific FIFO queue. At the
end of a run, queued and active requests are reported separately from completed
work.

## Method and cycle design

The [design page](design.html) records the method inputs/return values for
configuration loading, server assignment and ticking, numeric IP checking,
queue processing, scaling, statistics, and `main()`. Each cycle follows this
order: generate and firewall-check arrivals; evaluate scaling and cooldown;
tick each server that was already busy at the start of processing; record any
completion; dispatch each queue's FIFO requests only to its matching pool's
idle servers; then advance the completed-cycle count. Newly assigned work
therefore gets its first processing tick on the next cycle. A five-cycle
request assigned after cycle 0 completes after its fifth processing tick.
`WebServer::tick()` returns true exactly on the cycle a request completes. The
load balancer owns each server through `std::unique_ptr`, so successful scaling
destroys removed idle server objects.

Run focused regression tests with:

```sh
make test
```

They cover five-cycle processing, FIFO dispatch, scale-up and cooldown,
scale-down safety/minimum capacity, CIDR boundaries, blocked initial and later
requests, accounting, and invalid configuration.

## Documentation and design

[`design.html`](design.html) is a self-contained workflow/design page suitable for local
preview and upload to a personal web-hosting folder. `Doxyfile` configures
Doxygen to produce HTML API documentation in `docs/api`. Generate it with
`doxygen Doxyfile` when Doxygen is installed. The generated HTML can then be
uploaded to the required `people.tamu.edu` account; hosting credentials and
uploading are intentionally left to the project owner.
