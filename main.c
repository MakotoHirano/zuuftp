#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define TYPE_ASCII 'A'
#define TYPE_BINARY 'I'

#define LOCAL_TO_REMOTE 1
#define REMOTE_TO_LOCAL 2

#define DEFAULT_PORT "21"
#define TIMEOUT_SEC 2

#define DEBUG 0

typedef struct RemotePath {
	char *host;
	char *user;
	char *path;
} RemotePath;

typedef struct FtpResponse {
	int code;
	char *message;
} FtpResponse;

typedef struct FtpPasv {
	char *firstOctet;
	char *secondOctet;
	char *thirdOctet;
	char *fourthOctet;
	char *fifthOctet;
	char *sixthOctet;
} FtpPasv;

typedef struct FtpDestination {
	char *host;
	int port;
} FtpDestination;

static void _setArg(char **s, char *t);
static RemotePath _parseScheme(char *scheme);
static FtpResponse _parseResponse(char *message);
static FtpDestination _parsePasv(char *response);
static int _openConnectionSp(char *host, char *port);
static int _openConnectionIp(char *host, int port);
static int _login(int sock, char *user, char *pass);
static int _retr(int sock, char type, char *src, char *dst);
static int _stor(int sock, char type, char *src, char *dst);
static int _errorHandleFtp(FtpResponse ftpResponse);
static void _setFtpTimeout(int *sock, int sec);
static void _usage();

int main(int argc, char* argv[]) {

	int opt;
	char *host = NULL, *port = NULL;
	char *user = NULL, *pass = NULL;
	char *src = NULL, *dst = NULL;
	char type = 0;
	char recursive = 0, stream = 0;
	RemotePath remotePath;

	while((opt = getopt(argc, argv, "p:P:t:R:")) != -1) {
		switch (opt) {
			case 'p':
				_setArg(&port, optarg);
				break;
			case 'P':
				_setArg(&pass, optarg);
				break;
			case 'R':
				recursive = 1;
				break;
			case 't':
				if (strncmp("ascii", optarg, 5) == 0) {
					type = TYPE_ASCII;
				} else if (strncmp("binary", optarg, 6) == 0) {
					type = TYPE_BINARY;
				} else {
					return 1;
				}
				break;
			default:
				return 1;
		}
	}

	if (optind != argc-2) {
		_usage(); return 1;
	}

	src = argv[optind];
	dst = argv[optind+1];

	// default setting.
	if (type == 0) {
		type = TYPE_BINARY;
	}
	if (port == NULL) {
		port = DEFAULT_PORT;
	}
	if (pass == NULL) {
		pass = getpass("input password of ftp user: ");
	}

	if (DEBUG) {
		printf("port: %s\n", port);
		printf("pass: %s\n", pass);
		printf("type: %c\n", type);
	}

	if (strstr(src, "@") != NULL) {
		stream = REMOTE_TO_LOCAL;
		remotePath = _parseScheme(src);

	} else if (strstr(dst, "@") != NULL) {
		if (stream == REMOTE_TO_LOCAL) { _usage(); return 1; }
		stream = LOCAL_TO_REMOTE;
		remotePath = _parseScheme(dst);

	} else {
		_usage(); return 1;
	}

	int sock = _openConnectionSp(remotePath.host, port);
	if (sock < 0) {
		fprintf(stderr, "failed to handshake.\n");
		printf("zuuftp failed.\n");
		return 1;
	}
	if (_login(sock, remotePath.user, pass) < 0) {
		printf("zuuftp failed.\n");
		return 2;
	}

	// RETR or STOR
	if (stream == REMOTE_TO_LOCAL) {
		if (_retr(sock, type, remotePath.path, dst) < 0) {
			printf("zuuftp failed.\n");
			return 3;
		}
	} else {
		if (_stor(sock, type, src, remotePath.path) < 0) {
			printf("zuuftp failed.\n");
			return 4;
		}
	}

	close(sock);
	printf("zuuftp succeeded.\n");
	return 0;
}

static void _usage() {
	printf("usage\n");
	printf(" zuuftp [-p port] [-P password] [user@host:]file1 [user@host:]file2\n");
}

static int _openConnectionSp(char *host, char *port) {
	return _openConnectionIp(host, atoi(port));
}

static int _openConnectionIp(char *host, int port) {
	struct sockaddr_in addr;

	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_len = sizeof(struct sockaddr_in);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if (inet_aton(host, &addr.sin_addr) == -1) {
		perror("failed to open connection");
		return -1;
	}

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(sock, (struct sockaddr *) &addr, sizeof(struct sockaddr_in)) == -1) {
		printf("errno: %d\n", errno);
		perror("failed to connecting server");
		return -2;
	}

	return sock;
}

static int _login(int sock, char *user, char *pass) {
	char buf[512];
	int len, n;
	FtpResponse ftpRes;

	memset(buf, 0, sizeof(buf));

	// read accept message.
	n = read(sock, buf, sizeof(buf));
	if (DEBUG) { printf("%s\n", buf); }
	if (_errorHandleFtp(_parseResponse(buf)) < 0) {
		return -1;
	}

	// do user command.
	sprintf(buf, "USER %s\n", user);
	n = write(sock, buf, strlen(buf));
	n = read(sock, buf, sizeof(buf));
	if (DEBUG) { printf("%s", buf); }
	if (_errorHandleFtp(_parseResponse(buf)) < 0) {
		return -2;
	}

	// do pass command.
	sprintf(buf, "PASS %s\n", pass);
	n = write(sock, buf, strlen(buf));
	n = read(sock, buf, sizeof(buf));
	if (DEBUG) { printf("%s", buf); }
	if (_errorHandleFtp(_parseResponse(buf)) < 0) {
		return -3;
	}

	return 0;
}

static int _errorHandleFtp(FtpResponse ftpResponse) {
	if (ftpResponse.code >= 400) {
		printf("error from server. %s", ftpResponse.message);
		return -1;
	}

	return 0;
}

static int _retr(int controlSock, char type, char *src, char *dst) {
	char buf[512];
	int len, n;

	FILE *fp = fopen(dst, "wb");
	if (fp == NULL) {
		perror("failed to fopen");
		return -1;
	}

	// do type command.
	sprintf(buf, "TYPE %c\n", type);
	n = write(controlSock, buf, strlen(buf));
	n = read(controlSock, buf, sizeof(buf));
	if (DEBUG) { printf("%s", buf); }
	if (_errorHandleFtp(_parseResponse(buf)) < 0) {
		return -2;
	}

	// do pasv command.
	sprintf(buf, "PASV\n");
	n = write(controlSock, buf, strlen(buf));
	n = read(controlSock, buf, sizeof(buf));
	if (DEBUG) { printf("%s", buf); }
	if (_errorHandleFtp(_parseResponse(buf)) < 0) {
		return -3;
	}

	FtpResponse ftpRes = _parseResponse(buf);
	FtpDestination ftpDest = _parsePasv(ftpRes.message);

	// do retr command.
	sprintf(buf, "RETR %s\n", src);
	n = write(controlSock, buf, strlen(buf));

	_setFtpTimeout(&controlSock, TIMEOUT_SEC);
	n = read(controlSock, buf, sizeof(buf));
	if (n > 0) {
		if (_errorHandleFtp(_parseResponse(buf)) < 0) {
			return -4;
		}
	}

	// connect to data sock.
	memset(buf, 0, sizeof(buf));
	int dataSock = _openConnectionIp(ftpDest.host, ftpDest.port);
	while((n = read(dataSock, buf, sizeof(buf))) > 0) {
		fwrite(buf, n, 1, fp);
	}

	fclose(fp);
	close(dataSock);

	return 0;
}

static int _stor(int controlSock, char type, char *src, char *dst) {
	char buf[512];
	int len, n;

	FILE *fp = fopen(src, "rb");
	if (fp == NULL) {
		perror("failed to fopen");
		return -1;
	}

	// do type command.
	sprintf(buf, "TYPE %c\n", type);
	n = write(controlSock, buf, strlen(buf));
	n = read(controlSock, buf, sizeof(buf));
	if (DEBUG) { printf("%s", buf); }
	if (_errorHandleFtp(_parseResponse(buf)) < 0) {
		return -2;
	}

	// do pasv command.
	sprintf(buf, "PASV\n");
	n = write(controlSock, buf, strlen(buf));
	n = read(controlSock, buf, sizeof(buf));
	if (DEBUG) { printf("%s", buf); }
	if (_errorHandleFtp(_parseResponse(buf)) < 0) {
		return -3;
	}

	FtpResponse ftpRes = _parseResponse(buf);
	FtpDestination ftpDest = _parsePasv(ftpRes.message);

	// do retr command.
	sprintf(buf, "STOR %s\n", dst);
	n = write(controlSock, buf, strlen(buf));

	// try to read error message before data transfer.
	_setFtpTimeout(&controlSock, TIMEOUT_SEC);
	n = read(controlSock, buf, sizeof(buf));
	if (n > 0) {
		if (_errorHandleFtp(_parseResponse(buf)) < 0) {
			return -4;
		}
	}

	// connect to data sock.
	memset(buf, 0, sizeof(buf));
	int dataSock = _openConnectionIp(ftpDest.host, ftpDest.port);
	while((n = fread(buf, sizeof(char), sizeof(buf), fp)) > 0) {
		write(dataSock, buf, n);
	}

	fclose(fp);
	close(dataSock);

	return 0;
}


static RemotePath _parseScheme(char *scheme) {
	int i, j, k, len;
	char *user, *host, *path;
	RemotePath remotePath;

	// find user.
	for(i=0; i<strlen(scheme); i++) {
		if (scheme[i] == '@') {
			break;
		}
	}

	len = i+1;
	user = malloc(sizeof(char) * len);
	strncpy(user, scheme, len-1);
	user[len-1] = '\0';

	// find host.
	for(j=i; j<strlen(scheme); j++) {
		if (scheme[j] == ':') {
			break;
		}
	}

	len = j-i;
	host = malloc(sizeof(char) * len);
	strncpy(host, scheme+i+1, len-1);
	host[len-1] = '\0';

	// find path.
	len = strlen(scheme) - j;
	path = malloc(sizeof(char) * len);
	strncpy(path, scheme+j+1, len-1);
	path[len-1] = '\0';

	remotePath.user = user;
	remotePath.host = host;
	remotePath.path = path;

	return remotePath;
}

static FtpResponse _parseResponse(char *response) {
	int i, len;
	FtpResponse ftpRes;

	for (i=0; i<strlen(response); i++) {
		if (response[i] == ' ') {
			char *code, *message;
			code = malloc(sizeof(char) * (i+1));
			strncpy(code, response, i);
			*(code+i) = '\0';

			len = strlen(response) - i;
			message = malloc(sizeof(char) * len);
			strncpy(message, response+i+1, len-1);

			ftpRes.code = atoi(code);
			ftpRes.message = message;
			break;
		}
	}

	return ftpRes;
}

static FtpDestination _parsePasv(char *response) {
	int i, s, e;
	FtpPasv ftpPasv;
	FtpDestination ftpDest;

	memset(&ftpPasv, 0, sizeof(ftpPasv));
	memset(&ftpDest, 0, sizeof(ftpDest));

	for (i=0; i<strlen(response); i++) {
		if (response[i] == '(') {
			s = i;
		}
		if (response[i] == ')') {
			e = i;
		}
	}

	int len = e-s;
	char *serverInfo = malloc(sizeof(char) * len);
	strncpy(serverInfo, response+s+1, len);
	serverInfo[len - 1] = '\0';

	char *p;
	p = strtok(serverInfo, ","); ftpPasv.firstOctet = p;
	p = strtok(NULL, ","); ftpPasv.secondOctet = p;
	p = strtok(NULL, ","); ftpPasv.thirdOctet = p;
	p = strtok(NULL, ","); ftpPasv.fourthOctet = p;
	p = strtok(NULL, ","); ftpPasv.fifthOctet = p;
	p = strtok(NULL, ","); ftpPasv.sixthOctet = p;

	len = strlen(ftpPasv.firstOctet) + strlen(ftpPasv.secondOctet) + strlen(ftpPasv.thirdOctet) + strlen(ftpPasv.fourthOctet) + 4;
	ftpDest.host = malloc(sizeof(char) * len);
	sprintf(ftpDest.host, "%s.%s.%s.%s", ftpPasv.firstOctet, ftpPasv.secondOctet, ftpPasv.thirdOctet, ftpPasv.fourthOctet);
	ftpDest.port = atoi(ftpPasv.fifthOctet) * 256 + atoi(ftpPasv.sixthOctet);

	return ftpDest;
}

static void _setFtpTimeout(int *sock, int sec) {
	struct timeval tv;
	tv.tv_sec = sec;
	tv.tv_usec = 0;
	setsockopt(*sock, SOL_SOCKET, SO_RCVTIMEO, (char*) &tv, sizeof(tv));
}

static void _setArg(char **s, char *t) {
	int charlen = strlen(t);
	*s = malloc(sizeof(char) * (charlen+1));
	strncpy(*s, t, charlen);
	*(*s+charlen) = '\0';
}
