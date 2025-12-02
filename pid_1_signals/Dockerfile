FROM alpine:latest

# Copy both binaries
COPY wrapper /wrapper
COPY sigterm_test /sigterm_test

# Default: wrapper in --default
CMD ["/wrapper", "--pass", "/sigterm_test"]
