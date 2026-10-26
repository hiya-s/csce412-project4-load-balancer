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

Use flags for a non-interactive run. The assignment's requested 20-server,
100,000-cycle quiet run is:

```sh
./load_balancer --servers 20 --cycles 100000 --quiet --log project4-summary.log
```

Use `--verbose` instead of `--quiet` to print meaningful completions, blocked
requests, scaling events, and periodic progress snapshots. Routine accepted
and dispatched request events are intentionally omitted so logs remain useful
at high traffic rates. Set `progress_snapshot_interval` to control snapshot
frequency, or zero to disable snapshots. Quiet mode prints the starting and
ending summaries; both modes write a summary to the log file.
Use `--help` to show all command-line flags. No executable or generated log
needs to be included in a source-only submission archive.

The checked-in `project4-summary.log` is the quiet 20-server, 100,000-cycle
sample run. The default arrival probability is 0.65, with one arrival per
successful arrival event; service times are 2–10 cycles and scaling has a
10-cycle cooldown. This produces a starting queue of 2,000 for the requested
20-server run while allowing the pool to shrink or grow with demand.

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
work. The summary checks
`generated = blocked + completed + queued + still processing`.

## Method and cycle design

The [design page](design.html) records the method inputs/return values for
configuration loading, server assignment and ticking, numeric IP checking,
queue processing, scaling, statistics, and `main()`. Each cycle follows this
order: generate and firewall-check arrivals; evaluate scaling and cooldown;
tick each server that was already busy at the start of processing; record any
completion; dispatch each queue's FIFO requests only to its matching pool's idle servers;
then advance the completed-cycle count. Newly assigned work therefore gets its
first processing tick on the next cycle. A five-cycle request assigned after cycle 0 completes
after the tick on cycle 5. `WebServer::tick()` returns true exactly on the
cycle a request completes. The load balancer owns each server through
`std::unique_ptr`, so successful scaling destroys removed idle server objects.

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
