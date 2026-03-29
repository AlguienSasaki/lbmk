Mkhtemp - Hardened mktemp
-------------------------

Just like normal mktemp, but hardened.

Create new files and directories randomly as determined by
the user's TMPDIR, or fallback. These temporary files and
directories can be generated from e.g. shell scripts, running
mkhtemp. There is also a library that you could use in your
program. Portable to Linux and BSD. **WORK IN PROGRESS.
This is a very new project. Expect bugs - a stable release
will be announced, when the code has matured.**

A brief summary of *why* mkhtemp is more secure (more
details provided later in this readme - please also
read the source code):

Detect and mitigate symlink attacks, directory access
race conditions, unsecure TMPDIR (e.g. bad enforce sticky
bit policy on world writeable dirs), implement in user
space a virtual sandbox (block directory escape and resolve
paths by walking from `/` manually instead of relying on
the kernel/system), voluntarily error out (halt all
operation) if accessing files you don't own - that's why
sticky bits are checked for example, even when you're root.

It... blocks symlinks, relative paths, attempts to prevent
directory escape (outside of the directory that the file
you're creating is in), basically implementing an analog
of something like e.g. unveil, but in userspace!

Mkhtemp is designed to be the most secure implementation
possible, of mktemp, offering a heavy amount of hardening
over traditional mktemp. Written in C99, and the plan is
very much to keep this code portable over time - patches
very much welcome.

i.e. please read the source code

```
/*
 * WARNING: WORK IN PROGRESS.
 * Do not use this software in
 * your distro yet. It's ready
 * when it's ready. Read the src.
 *
 * What you see is an early beta.
 *
 * Please do not merge this in
 * your Linux distro package repo
 * yet (unless maybe you're AUR).
 */
```

Supported mktemp flags:

```
mkhtemp: usage: mkhtemp [-d] [-p dir] [template]

 -p DIR       <-- set directory, overriding TMPDIR
 -d           <-- make a directory instead of a file
 -q           <-- silence errors (exit status unchanged)
```

The rest of them will be added later (the same ones
that GNU and BSD mktemp implement). With these options,
you can generate files/directories already.

You can also write a template at the end. e.g.

```
mkhtemp -d -p path/to/directory vickysomething_XXXXXXXXXXX
```

On most sane/normal setups, the program should already
actually work, but please know that it's very different
internally than every other mktemp implementation.

Read the source code if you're interested. As of this
time of writing, mkhtemp is very new, and under
development. A stable release will be announced when ready.

### What does mkhtemp do differently?

This software attempts to provide mitigation against
several TOCTOU-based
attacks e.g. directory rename / symlink / re-mount, and
generally provides much higher strictness than previous
implementations such as mktemp, mkstemp or even mkdtemp.
It uses several modern features by default, e.g. openat2
and `O_TMPFILE` (plus `O_EXCL`) on Linux, with additional
hardening; BSD projects only have openat so the code uses
that there, but some (not all) of the kinds of checks
Openat2 enforces are done manually (in userspace).

File system sandboxing in userspace (pathless discovery,
and operations are done only with FDs). At startup, the
root directory is opened, and then everything is relative
to that.

Many programs rely on mktemp, and they use TMPDIR in a way
that is quite insecure. Mkhtemp intends to change that,
quite dramatically, with: userspace sandbox (and use OS
level options e.g. OBSD pledge where available), constant
identity/ownership checks on files, MUCH stricter ownership
restrictions (e.g. enforce sticky bit policy on world-
writeable tmpdirs), preventing operation on other people's
files (only your own files) - even root is restricted,
depending on how the code is compiled. Please read the code.

Basically, the gist of it is that normal mktemp *trusts*
your system is set up properly. It will just run however
you tell it to, on whatever directory you tell it to, and
if you're able to write to it, it will write to it.
Some implementations (e.g. OpenBSD one) do some checks,
but not all of them do *all* checks. The purpose of
mkhtemp is to be as strict as possible, while still being
reliable enough that people can use it. Instead of catering
to legacy requirements, mkhtemp says that systems should
be secure. So if you're running in an insecure environment,
the goal of mkhtemp is to *exit* when you run it; better
this than files being corrupted.

Security and reliability are the same thing. They both
mean that your computer is behaving as it should, in a
manner that you can predict.

It doesn't matter how many containers you have, or how
memory-safe your programming language is, the same has
been true forever: code equals bugs, and code usually
has the same percentage of bugs, so more code equals
more bugs. Therefore, highly secure systems (such as
OpenBSD) typically try to keep their code as small and
clean as possible, so that they can audit it. Mkhtemp
assumes that your system is hostile, and is designed
accordingly.

What?
-----

This is the utility version, which makes use of the also-
included library. No docs yet - source code are the docs,
and the (ever evolving, and hardening) specification.

This was written from scratch, for use in nvmutil, and
it is designed to be portable (BSD, Linux). Patches
very much welcome.

Caution
-------

This is a new utility. Expect bugs.

```
WARNING: This is MUCH stricter than every other mktemp
         implementation, even more so than mkdtemp or
         the OpenBSD version of mkstemp. It *will* break,
         or more specifically, reveal the flaws in, almost
         every major critical infrastructure, because most
         people already use mktemp extremely insecurely.
```

This tool is written by me, for me, and also Libreboot, but
it will be summitted for review to various Linux distros
and BSD projects once it has reached maturity.

### Why was this written?

Atomic writes were implemented in nvmutil (Libreboot's
Intel GbE NVM editor), but one element remained: the
program mktemp, itself, which has virtually no securitty
checks whatsoever. GNU and BSD implementations use
mkstemp now, which is a bit more secure, and they offer
additional hardening, but I wanted to be reasonably
assured that my GbE files were not being corrupted in
any way, and that naturally led to writing a hardened
tool. It was originally just going to be for nvmutil,
but then it became its own standard utility.

Existing implementations of mktemp just simply do not
have sufficient checks in place to prevent misuse. This
tool, mkhtemp, intentionally focuses on being secure
instead of easy. For individuals just running Linux on
their personal machine, it might not make much difference,
but corporations and projects running computers for lots
of big infrastructure need something reliable, since
mktemp is just one of those things everyone uses.
Every big program needs to make temporary files.

But the real reason I wrote this tool is because, it's
fun, and because I wanted to challenge myself.

Roadmap
-------

Some things that are in the near future for mkhtemp
development:

Thoroughly document every known case of CVEs in the wild,
and major attacks against individuals/projects/corporations
that were made possible by mktemp - that mkhtemp might
have prevented. There are several.

More hardening; still a lot more that can be done, depending
on OS. E.g. integrate FreeBSD capsicum.

Another example: although usually reliable, comparing the
inode and device of a file/directory isn't by itself sufficient.
There are other checks that mkhtemp does; for example I could
implement it so that directories are more aggressively re-
opened by mkhtemp itself, mid-operation. This re-opening
would be quite expensive computationally, but it would then
allow us to re-check everything, since we store state from
when the program starts.

Tidy up the code: the current code was thrown together in
a week, and needs tidying. A proper specification should be
written, to define how it works, and then the code should
be auditted for compliance. A lot of the functions are
also quite complex and do a lot; they could be split up.

Right now, mkhtemp mainly returns a file descriptor and
a path, after operation, ironic given the methods it uses
while opening your file/dir. After it's done, you then have
to handle everything again. Mkhtemp could keep everything
open instead, and continue to provide verification; in
other words, it could provide a completely unified way for
Linux/BSD programs to open files, write to them atomically,
and close. Programs like Vim will do this for example, or
other text editors, but every program has its own way. So
what mkhtemp could do is provide a well-defined API alongside
its mktemp hardening. Efforts would be made to avoid
feature creep, and ensure that the code remains small and
nimble.

Compatibility mode: another thing is that mkhtemp is a bit
too strict for some users, so it may break some setups. What
it could do is provide a compatibility mode, and in this
mode, behave like regular mktemp. That way, it could become
a drop-in replacement on Linux distros (and BSDs if they
want it), while providing a more hardened version and
recommending that where possible.

~~Rewrite it in rust~~ (nothing against it though, I just like C99 for some reason)

Also, generally document the history of mktemp, and how
mkhtemp works in comparison.

Also a manpage.

Once all this is done, and the project is fully polished,
then it will be ready for your Linux distro. For now, I
just use it in nvmutil (and I also use it on my personal
computer).
