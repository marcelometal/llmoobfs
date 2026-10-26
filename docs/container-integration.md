# Using MoobFS from a container

Status: roadmap item C3 ([ADR-0022](decisions/0022-kernel-freeze-and-user-space-consumable-track.md)).
What was actually run is in [journal 0199](history/0199-container-recipe.md); the section
[What was tested](#what-was-tested) at the end summarizes it.

> **Experimental. Not for production.** The layer runs on the host's kernel, so a container
> does not isolate the host from a defect in it. Same rule as [operations.md](operations.md):
> a dedicated test host or VM only.

A container never mounts the layer. Mounting needs `CAP_SYS_ADMIN` in the host's initial user
namespace, and the module refuses mounts from a user namespace and idmapped mounts on purpose
([ADR-0011](decisions/0011-threat-and-credential-model.md)). The host mounts it, and each
container receives a bind mount of its agent's directory.

## 1. On the host

Install and mount as in [operations.md](operations.md), sections 1 to 5. Every agent has its
own OS user and a domain directory that it owns:

```sh
sudo useradd -u 1001 alice
sudo install -d -o alice -g alice -m 0700 /srv/memory/alice
```

## 2. The recipe: the agent's domain directory, under the owner's uid

```sh
docker run --rm --user 1001:1001 -v /srv/memory/alice:/memory <image> <agent>
podman run --rm --user 1001:1001 -v /srv/memory/alice:/memory <image> <agent>
```

- **The numeric uid and gid must be the ones that own the directory on the host.** Nothing maps
  them: the layer has no idmapped mounts, so the kernel judges the container's process as host
  uid 1001. The user name inside the image does not matter, and `/etc/passwd` in the image need
  not list it.
- Inside, `/memory` is the agent's memory: files are read and written as usual, and typed
  attributes are set with `setxattr` (or `moobfs-attr`). The kernel validates every value, and
  an invalid one fails with `EINVAL` exactly as on the host.
- **Queries work through this bind.** `QUERY2` judges the directory by its place in the layer,
  not by the mount point, so `moobfs query /memory kind eq string decision` (or the ioctl on
  `/memory`) answers inside the container. Copy the `moobfs` binary into the image or bind it
  read-only (`-v /usr/local/bin/moobfs:/usr/local/bin/moobfs:ro`).
- `moobfs info /memory` cannot answer: `GET_INFO` needs the mount root, which this bind does not
  reach, and the tool says so (exit 69). This is the open point of
  [client-contract.md](client-contract.md), section 7.
- Each agent gets its own container, uid and directory. A container started with the wrong uid
  for a directory gets `EACCES` from the kernel, for files and for queries.

## 3. Variant: the whole root read-only, beside the domain

```sh
docker run --rm --user 1001:1001 -v /srv/memory:/all:ro -v /srv/memory/alice:/memory <image> <agent>
```

This adds `GET_INFO` (`moobfs info /all`) and lets a process query any domain it may read
(`/all/<domain>`). The cost is that the top-level names become visible, as POSIX allows for a
directory with mode `0755`: the container sees that `bob` exists, though not what is inside.
Writes through `/all` fail with `EROFS`. Use it only when those names are not private.

## 4. What does not work

| Setup | Result | Why |
| --- | --- | --- |
| Mounting `moobfs_layer` inside the container | refused | needs `CAP_SYS_ADMIN` in the initial user namespace (no `FS_USERNS_MOUNT`) |
| A user namespace that remaps the container's uids (`--userns`, `--uidmap`, Docker `userns-remap`) | the files keep their host owners, so the remapped process is refused by ordinary permissions | no idmapped mounts to translate them |
| An idmapped bind (`podman ... -v dir:/memory:idmap`) | Podman 5.4.2 starts the container, but the bind is a plain one: not marked `idmapped`, owners not translated, so the remapped process is refused as in the row above | the layer does not set `FS_ALLOW_IDMAP`, so the kernel refuses the idmapping |
| `GET_INFO` through a bind of a subdirectory | refused (`ENOTTY`; `moobfs info` exit 69) | `GET_INFO` answers only on the mount root |
| The mount-wide `QUERY` through any bind, for an ordinary user | refused (`EPERM` with domains on) | it would traverse every user's domain |

## 5. Rootless Podman, under the agent's own uid

A rootless container cannot mount the layer, but the agent's user can run its own Podman on a
layer the host mounted, keeping its uid inside the container:

```sh
# as alice (uid 1001), with a user session (loginctl enable-linger alice)
podman run --rm --userns=keep-id -v /srv/memory/alice:/memory <image> <agent>
```

`--userns=keep-id` maps alice's host uid to the same uid in the container, so the kernel judges
the process as uid 1001 on the layer, exactly as in section 2. Other rootless modes map the
container's uids to subordinate host uids (`/etc/subuid`), which own nothing on the layer, and
are refused like any remapped user namespace. Rootless Docker was not tested.

## What was tested

Two runs of `make container-test` ([journal 0199](history/0199-container-recipe.md)), in a
disposable Debian 13 VM with kernel 6.12.111+deb13-amd64, Docker 26.1.5 (Debian's `docker.io`)
and Podman 5.4.2; run 1 under QEMU TCG, run 2 under KVM (`MOOB_HOST_QEMU=1`):

- sections 2 and 3 with both runtimes: read, write and `setxattr` through the domain bind, an
  invalid value refused, `QUERY2` answered, `moobfs info` refused with exit 69, a second agent
  under uid 1002 seeing only its directory, the wrong uid refused (exit 77), and the read-only
  root bind;
- section 4: a remapped uid 1001 (`--uidmap 0:200000:65536`) refused; `:idmap` observed as a
  plain bind without translation (run 2);
- section 5: alice's rootless Podman with `--userns=keep-id` queried her domain (run 2).

Not tested: rootless Docker, Docker's `userns-remap` daemon mode, Kubernetes and other
runtimes, SELinux-labelled binds (`:z`, `:Z`; see [when-not-to-use.md](when-not-to-use.md)),
any kernel other than the one above.
