FROM golang:1.25-alpine AS builder
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY *.go ./
RUN CGO_ENABLED=0 go build -trimpath -ldflags "-s -w" -o /out/alert-bridge .

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=builder /out/alert-bridge /alert-bridge
USER 65532:65532
ENTRYPOINT ["/alert-bridge"]
