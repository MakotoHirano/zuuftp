### zuuftp

`zuuftp` is ftp cli client behaves like `scp` cli.
You can transfer files using ftp protocol with easy syntax.

#### server to client

when you want to fetch file from ftp server, the syntax will be like below.

```
zuuftp user@host:/path/to/file /local/path/to/file
```

#### client to server

otherwise, you can use almost same syntax uploading file to ftp server with syntax:

```
zuuftp /local/path/to/file user@host:/path/to/file
```

### Remarks

I'm planning implement more advanced features like implementing FTPS in future. But now, the socket communication is implemeted by raw stream (using just ftp). So I strongly recommend that You should not use this cli (means also ftp server) on public internet, but just for closed local network.

### Planning features

- ftps implementation
- directory recursive file transfer

### Environment
currently, only confirmed to work on Mac OSX 10.11

### Install
Just as ordinal makefile project. follow like below.

```
make
sudo make install
```
