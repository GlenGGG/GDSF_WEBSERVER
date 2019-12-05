# GDSF_WEBSERVER

A multi-thread webserver optimized with GDSF cache algorithm.

## Getting Started

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes. See deployment for notes on how to deploy the project on a live system.

### Prerequisites

What things you need to install the software and how to install them

```
You need to have gcc and make in your PATH.
```

### Installing

Clone this repository.

```
git clone https://github.com/GlenGGG/VFS.git
cd GDSF_WEBSERVER
```

Run make.

```
make
```
Now create a file called list, and write all the name of your resources into it. You can do it with a simple command:
```
ls the-path-to-your-resources > list
```

Run the webserver.

```
./webserver port path-to-your-website-root
```

Then, you should be able to see the webserver working on 127.0.0.1:port .

## Running the tests

You can test this webserver's performance through http_load.

```
http_load -parallel [number of concurrent connections] -seconds [time lenght of load test]
```
