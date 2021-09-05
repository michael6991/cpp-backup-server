/**
 * server.h
 *
 */
#ifndef SERVER_H_
#define SERVER_H_

/* possible operation requests */
#define BACKUP_FILE (100)
#define GET_FILE (200)
#define ERASE_FILE (201)
#define GET_BACKUP_LIST (202)


/* return codes*/
#define GET_FILE_SUCCESS (210) // get file from backup was successful
#define GET_BACKUP_LIST_SUCCESS (211)  // get list of backed files was successful
#define BACKUP_FILE_SUCCESS (212) // backup of file was successful
#define ERASE_FILE_SUCCESS (212) // erase of file was successful
#define FILE_NOT_FOUND (1001) // backup directory does not have this file
#define NO_FILES_FOR_CLIENT (1002) // backup directory for this user is empty
#define GENERAL_ERROR (1003) // general problem with the server


/* maximum size of chunk to read from client's request message */
#define MAX_LENGTH (1024)

/* exact amount of bytes in header without filename */
#define HEADER_SIZE (8)

/* server's and client's version */
#define VERSION_SERVER (1)
#define VERSION_CLIENT (1)

/* the path to the parent directory that holds backup directories for all the clients */
/* this path is within the directory of the server.exe */
#define SERVER_BACKUP_PARENT_DIR ("C:\\backup_svr\\")


/*  client's request message */
struct Request
{
	/* header */
	uint32_t userid = 0;
	uint8_t version = 0;
	uint8_t op = 0;
	uint16_t nameLen = 0;
	std::string filename = "";

	/* payload */
	uint32_t size = 0;
	uint8_t* payload = 0;
};


/*  server's response message */
struct Response
{
	/* header */
	uint8_t version = 0;
	uint16_t status = 0;
	uint16_t nameLen = 0;
	std::string filename = "";

	/* payload */
	uint32_t size = 0;
	uint8_t* payload = 0;
};


#endif /* server.h */

