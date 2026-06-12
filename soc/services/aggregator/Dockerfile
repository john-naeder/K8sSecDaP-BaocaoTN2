# Build stage
FROM golang:1.25-alpine AS builder
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY *.go ./
ARG VERSION=dev
RUN CGO_ENABLED=0 go build -trimpath -ldflags "-s -w -X main.version=${VERSION}" -o /out/aggregator .

# Runtime stage — distroless static
FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=builder /out/aggregator /aggregator
USER 65532:65532
EXPOSE 9100
ENTRYPOINT ["/aggregator"]
