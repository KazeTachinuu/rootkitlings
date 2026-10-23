# Build context: repository root
#   docker build -f product/operator/Dockerfile -t rklings-operator .

FROM node:22-alpine AS webapp
WORKDIR /src
COPY lib/webapp/package.json lib/webapp/package-lock.json ./
RUN npm ci
COPY lib/webapp/ .
RUN npm run build

FROM golang:1.27-alpine AS build
WORKDIR /src
COPY product/operator/go.mod product/operator/go.sum ./
RUN go mod download
COPY product/operator/ .
RUN CGO_ENABLED=0 go build -o /operator ./cmd/server

FROM alpine:3.21
COPY --from=build /operator /usr/bin/operator
COPY --from=webapp /src/dist /webapp
ENV RKLINGS_WEB_DIR=/webapp
EXPOSE 443 8080
ENTRYPOINT ["operator"]
