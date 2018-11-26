# Simple multithreading caching proxy for POSIX systems

This proxy supports only HTTP/1.0.

Requests/Responses using HTTP/1.1 will be interpreted as HTTP/1.0.

## Requirements

* UNIX system
* GNU Make

## Build

### Downloading sourse codes

```
git clone https://github.com/kirpichik/yx-proxy.git
cd yx-proxy
```

### Building

```
make
```

### Usage

```
./yx-proxy < port >
```

## Included dependencies

* [NodeJS/http-parser](https://github.com/nodejs/http-parser)

