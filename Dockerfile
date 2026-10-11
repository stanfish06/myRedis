FROM debian:trixie-slim AS build

RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ make \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY Makefile *.cpp *.h ./
RUN make

# Separate build and run stages so that the image is smaller
FROM debian:trixie-slim

RUN useradd --system --no-create-home redis

WORKDIR /app
COPY --from=build /src/server /src/client ./

USER redis
ENV PORT=1234
EXPOSE 1234

CMD ["./server"]
