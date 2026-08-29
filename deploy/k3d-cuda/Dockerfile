# k3s node image with the NVIDIA container runtime inside.
#
# The stock rancher/k3s image is busybox-based with no package manager, so the
# toolkit cannot be added to it directly. Instead the k3s filesystem is laid
# over a CUDA base that does have apt.
#
# The two images disagree about the root layout, which is what makes this a
# list of explicit COPYs rather than one `COPY / /`:
#   rancher/k3s     /bin and /lib are real directories
#   nvidia/cuda     /bin -> usr/bin, /lib -> usr/lib   (Ubuntu usr-merge)
# Copying a directory onto a symlink fails with "cannot copy to non-directory",
# so each source is mapped to its usr-merged destination.
#
# /etc is deliberately NOT copied: k3s ships a minimal passwd/group that would
# overwrite the ones apt just populated.
#
# k3s >= 1.27 scans PATH for nvidia-container-runtime at startup and registers
# it with containerd itself, so no config.toml.tmpl is needed — only the binary
# has to be present. RuntimeClass and device plugin are applied to the running
# cluster afterwards, not baked in.
ARG K3S_TAG=v1.33.4-k3s1
ARG CUDA_TAG=12.6.3-base-ubuntu24.04

FROM rancher/k3s:${K3S_TAG} AS k3s
FROM nvidia/cuda:${CUDA_TAG}

RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl gnupg2 \
 && curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey \
      | gpg --dearmor -o /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg \
 && curl -fsSL https://nvidia.github.io/libnvidia-container/stable/deb/nvidia-container-toolkit.list \
      | sed 's#deb https://#deb [signed-by=/usr/share/keyrings/nvidia-container-toolkit-keyring.gpg] https://#g' \
      > /etc/apt/sources.list.d/nvidia-container-toolkit.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends nvidia-container-toolkit \
 && rm -rf /var/lib/apt/lists/*

# Busybox applets land in /usr/bin and shadow the Ubuntu coreutils of the same
# name. That is expected for a k3s node. The toolkit binaries installed above
# have distinct names and survive.
COPY --from=k3s /bin       /usr/bin
COPY --from=k3s /lib       /usr/lib
COPY --from=k3s /usr/share /usr/share
# /var/run is a symlink to /run on Ubuntu and a real directory in k3s, so the
# two writable trees are copied individually rather than /var wholesale.
COPY --from=k3s /var/lib   /var/lib

VOLUME /var/lib/kubelet
VOLUME /var/lib/rancher/k3s
VOLUME /var/lib/cni
VOLUME /var/log

ENV PATH="$PATH:/bin/aux"

ENTRYPOINT ["/bin/k3s"]
CMD ["agent"]
