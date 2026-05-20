FROM ubuntu:22.04

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        && rm -rf /var/lib/apt/lists/* && mkdir -p /app/in

RUN dd if=/dev/urandom of=/app/in/1.bin bs=10M count=1
RUN dd if=/dev/urandom of=/app/in/2.bin bs=10M count=1
RUN dd if=/dev/urandom of=/app/in/3.bin bs=10M count=1
RUN dd if=/dev/urandom of=/app/in/4.bin bs=10M count=1
WORKDIR /app

COPY . .

RUN make

RUN make test

CMD ["bash"]