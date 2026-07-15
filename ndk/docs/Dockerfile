FROM sphinxdoc/sphinx-latexpdf:8.2.3@sha256:8fc7894c1961018ceaedf79d5dc3c4d74c1b0ea9c6a267ed65efa9596d3d17d0

USER root

RUN apt-get update \
    && apt-get install -y --no-install-recommends doxygen graphviz \
    && rm -rf /var/lib/apt/lists/*

COPY requirements.txt /tmp/astra-ndk-doc-requirements.txt
RUN python -m pip install --no-cache-dir \
    -r /tmp/astra-ndk-doc-requirements.txt

WORKDIR /workspace
