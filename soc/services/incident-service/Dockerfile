FROM golang:1.25-alpine AS builder
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY . .
ARG VERSION=dev
RUN CGO_ENABLED=0 go build -trimpath -ldflags "-s -w -X main.version=${VERSION}" \
    -o /out/incident-service ./cmd/server

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=builder /out/incident-service /incident-service
USER 65532:65532
EXPOSE 8080 9101
ENTRYPOINT ["/incident-service"]
