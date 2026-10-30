# RFC862

Echo Protocol server ([RFC 862](https://www.rfc-editor.org/rfc/rfc862)): sends back everything it receives over TCP and UDP on port 7, IPv4 and IPv6.

## Download

Statically linked binaries for any x86_64 or aarch64 Linux distribution: [Releases](https://github.com/artyom-fedosov/RFC862/releases).

## Run

The program takes no options. Binding port 7 requires the `CAP_NET_BIND_SERVICE` capability.

```sh
rfc862
```

It stops cleanly on `SIGINT`, `SIGTERM`, `SIGHUP` or `SIGQUIT`.

## Usage

```sh
nc localhost 7
```
