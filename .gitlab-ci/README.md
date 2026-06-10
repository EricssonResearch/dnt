# CI info

DNT automated testing built upon a Gitlab CI pipeline.
This contains steps like verify the workflow syntax (`.gitlab-ci.yml`),
generating the container image for testing, then run
the tests (unit and integration) itself.

## Runner config

Tests executed by a Gitlab runner, which is installed to an on-premise Ubuntu/Debian server.
The gitlab-runner must be installed on the machine.
The process of this detailed on Gitlab docs: https://docs.gitlab.com/runner/install/linux-manually/
This will create a new user called `gitlab-runner` which used as the context.
The user should be added to sudoers group as well as to `docker` group, e.g.:

```
sudo usermod -aG docker gitlab-runner
sudo usermod -aG sudoers gitlab-runner
```

### Obtain the mutual TLS certs

The deployed Gitlab server use mTLS auth for the runners.
How to obtain the certificates for that is depending on deployment details.
For our case it uses a tool called `rcli`.
After `rcli login` and authentication, it will download the certs:

```
/home/gitlab-runner/.devops/client.pem
/home/gitlab-runner/.devops/client-key.pem

# validation (optional)
sudo openssl x509 -in /home/gitlab-runner/.devops/client.pem -text -noout
```

### Register the runner

In the current pipline, we need two runners:
a Docker runner, for build, unit and integration tests,
and a shell runner for generate the image for those.
To create/register a runner, a __token__ is needed which can be obtained from Gitlab.
On the project page, go to the CI/CD settings and obtain the base command.
The URL for creating a new runner looks like the following (example):

`https://gitlab.example.com/example-group/example-team/example-repo/-/runners/new`

In the tags field, use clear naming regarding shell and docker runners.
For example `runner-docker` and `runner-shell` in our case.
Finally click on __Create runner__ there you will see the token,
which looks like the following: `glrt-XXXXXX...`.
This token needed so copy it, then login as the `gitlab-user`
for example `sudo su gitlab-user`

Now register the Docker runner with the following command:

```
gitlab-runner register --url https://gitlab.example.com --token glrt-XXXXXX --executor docker --privileged --tls-cert-file /home/gitlab-runner/.devops/client.pem --tls-key-file /home/gitlab-runner/.devops/client-key.pem --name runner-docker
```

Then register the shell executor runner as well with a new token:

```
gitlab-runner --debug register --url https://gitlab.example.com --token glrt-YYYYYY --executor shell --tls-cert-file /home/gitlab-runner/.devops/client.pem --tls-key-file /home/gitlab-runner/.devops/client-key.pem --name runner-shell
```


The configuration in `/etc/gitlab-runner/config.toml` will look like the following:

```toml
oncurrent = 1
check_interval = 0
connection_max_age = "15m0s"
shutdown_timeout = 0

[session_server]
  session_timeout = 1800

[[runners]]
  name = "runner-docker"
  url = "https://gitlab.example.com"
  id = 615928
  token = "glrt-XXXXXX"
  token_obtained_at = 2025-05-29T12:34:45Z
  token_expires_at = 0001-01-01T00:00:00Z
  tls-cert-file = "/home/gitlab-runner/.devops/client.pem"
  tls-key-file = "/home/gitlab-runner/.devops/client-key.pem"
  executor = "docker"
  [runners.cache]
    MaxUploadedArchiveSize = 0
  [runners.docker]
    tls_verify = false
    image = "gcc"
    privileged = true
    disable_entrypoint_overwrite = false
    oom_kill_disable = false
    disable_cache = false
    volumes = ["/cache"]
    extra_hosts = ["gitlab.example.com:X.X.X.X"]
    allowed_pull_policies = ["if-not-present", "never"]
    pull_policy = ["if-not-present"]
    shm_size = 0
    network_mtu = 0

[[runners]]
  name = "runner-shell"
  privileged = true
  url = "https://gitlab.example.com"
  id = 617612
  token = "glrt-YYYYYY"
  token_obtained_at = 2025-11-13T10:06:38Z
  token_expires_at = 0001-01-01T00:00:00Z
  tls-cert-file = "/home/gitlab-runner/.devops/client.pem"
  tls-key-file = "/home/gitlab-runner/.devops/client-key.pem"
  executor = "shell"
  [runners.cache]
    MaxUploadedArchiveSize = 0
    [runners.cache.s3]
```


Finally verify if there any issues, with running the runner service manually.
For that first stop the service, then start the runner:

```
sudo systemctl stop gitlab-runner.service
sudo gitlab-runner run --config /etc/gitlab-runner/config.toml --working-directory /home/gitlab-runner --service gitlab-runner --user gitlab-runner
```

With the runner should fetch jobs tagged with either `runner-docker` or `runner-shell`.
If thats the case, enable automatic start of the service and start it:

```
sudo systemctl enable gitlab-runner.service
sudo systemctl start gitlab-runner.service
```

## Gitlab CI workflow config

The CI related workflow config is in the root of the repo,
called `.gitlab-ci.yml` which is the default place to Gitlab to look for it.

The `build-image` stage, uses the shell executor to create the docker container image
and other stages use the docker executor with this image for testing.
