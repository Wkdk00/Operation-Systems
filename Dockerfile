FROM ubuntu:22.04

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        && rm -rf /var/lib/apt/lists/* && mkdir -p /app/in

WORKDIR /app

COPY . .

RUN make

RUN make test

CMD ["bash"]