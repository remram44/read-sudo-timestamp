This is a simple utility that reads sudo's timestamp file to figure out whether you have a valid timestamp.

This allows you to show a warning in your prompt if your shell currently allows you to run commands with sudo without a password.

# How to use

Build this executable with `make`, then install it with `sudo make install`. This will make it set-uid root so it can read sudo's timestamp files.

You can then put something like this in your shell's config file (example for bashrc):

```bash
PS1='`if read_sudo_timestamp 900 >/dev/null; then echo "SUDO "; fi`\u@\h:\w\$ '
```

`900` in the code above is sudo's timeout in seconds. I didn't try to get it from sudo's configuration file, it is easy enough for you to supply.

# Is a separate program really required?

Unfortunately sudo never provided a way to do this. On some versions you can abuse `sudo -v` to check whether there is a valid timestamp, however this counts as a failed login on some versions, sending the admin an email every time (thousands of emails if you have this in your prompt...), [sudo#131](https://github.com/sudo-project/sudo/issues/131). Additionally this would refresh the timestamp every time instead of only checking it.

The sudo project has been asked for a solution but refused, see [sudo#130](https://github.com/sudo-project/sudo/issues/130), so writing a separate program seemed the easiest way.
