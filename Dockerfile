FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
# protobuf-compiler-grpc provides /usr/bin/grpc_cpp_plugin
RUN apt-get update && apt-get install -y \
    build-essential \
    g++ \
    make \
    zlib1g-dev \
    gzip \
    dos2unix \
    ca-certificates \
    cmake \
    pkg-config \
    protobuf-compiler \
    protobuf-compiler-grpc \
    libprotobuf-dev \
    libgrpc++-dev \
    libgrpc-dev \
    libabsl-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy project files
COPY makefile ./
COPY src/ ./src/
COPY data/ ./data/
COPY run.sh ./
COPY res/.gitignore ./res/.gitignore
COPY bin/.gitignore ./bin/.gitignore
COPY distributed/ ./distributed/

# Normalize line endings and prepare directories
RUN mkdir -p bin res \
 && dos2unix run.sh \
 && find distributed -type f -not -path "*/build/*" -exec dos2unix {} + \
 && chmod +x run.sh

# Build original project
RUN make -s -j4

# Build distributed project
RUN cmake -S distributed -B distributed/build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build distributed/build -j

CMD ["/bin/bash"]