FROM debian:bookworm-slim AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc \
    libc6-dev \
    make \
    bash \
    curl \
    ca-certificates \
    pkg-config \
    libmicrohttpd-dev \
    && rm -rf /var/lib/apt/lists/*

# Download and install ONNX Runtime
ARG ONNXRUNTIME_VERSION=1.16.3
ARG TARGETARCH
RUN if [ "$TARGETARCH" = "arm64" ]; then \
        curl -sL https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-aarch64-${ONNXRUNTIME_VERSION}.tgz | tar xz -C /opt; \
        mv /opt/onnxruntime-linux-aarch64-${ONNXRUNTIME_VERSION} /opt/onnxruntime; \
    else \
        curl -sL https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz | tar xz -C /opt; \
        mv /opt/onnxruntime-linux-x64-${ONNXRUNTIME_VERSION} /opt/onnxruntime; \
    fi

ENV ONNXRUNTIME_ROOT=/opt/onnxruntime

WORKDIR /app
COPY . .

# Download yyjson if not present and build with ONNX support
RUN if [ ! -f third_party/yyjson/yyjson.c ]; then make yyjson; fi
RUN make clean && make ONNXRUNTIME_ROOT=/opt/onnxruntime all

# Download embedding model and vocabulary
RUN mkdir -p models && \
    curl -sL "https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/main/onnx/model.onnx" -o models/all-MiniLM-L6-v2.onnx && \
    curl -sL "https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/main/vocab.txt" -o models/vocab.txt

# Runtime image
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libmicrohttpd12 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy ONNX Runtime libraries
COPY --from=builder /opt/onnxruntime/lib /opt/onnxruntime/lib
ENV LD_LIBRARY_PATH=/opt/onnxruntime/lib

# Copy binaries and model
COPY --from=builder /app/build/bin/memory-service /app/memory-service
COPY --from=builder /app/build/bin/memory-cli /app/memory-cli
COPY --from=builder /app/models /app/models

# Create data directory
RUN mkdir -p /app/data/embeddings /app/data/relations

# Environment variables for configuration
ENV LOG_FORMAT=text

EXPOSE 8080

# Use shell form to expand environment variables
CMD /app/memory-service --data-dir /app/data --port 8080 --model /app/models/all-MiniLM-L6-v2.onnx --log-format ${LOG_FORMAT}
