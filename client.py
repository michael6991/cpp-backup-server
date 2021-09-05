from time import sleep
import socket
import random
import os

# possible operation requests
BACKUP_FILE = 100
GET_FILE = 200
ERASE_FILE = 201
GET_BACKUP_LIST = 202

# return codes
return_codes = {'GET_FILE_SUCCESS': 210,  # get file from backup was successful
                'GET_BACKUP_LIST_SUCCESS': 211,  # get list of backed files was successful
                'BACKUP_FILE_OR_ERASE_FILE_SUCCESS': 212,  # backup or erase of file was successful
                'FILE_NOT_FOUND': 1001,  # backup directory does not have this file
                'NO_FILES_FOR_CLIENT': 1002,  # backup directory for this user is empty
                'GENERAL_ERROR': 1003}  # general problem with the server


# chunk size for sending\receiving via sockets
buffer_size = 1024


def translate(code: int):
    """
    gets a return code that was sent by the server
    and returns the literal meaning of it according to
    return_codes dictionary that is defined above.
    :param code: integer return code
    :return:
    """
    for k, v in return_codes.items():
        if v == code:
            return k


class MySocket:
    """
    Implementation of client
    """

    def __init__(self, user_id=None, sock=None):
        if sock is None:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        else:
            self.sock = sock

        # unique user id upon creation
        if user_id is None:
            self.userid = random.randrange(0, 2 ** 32)  # 4 bytes
        else:
            self.userid = user_id
        print(f'\nClient id: {self.userid}')

        self._server_host = ''
        self._server_port = 0
        self.get_server_info()

        self.backup_list = self.get_backup_local_files_list()

        # server's response header will be saved here
        self.version = 1  # 1 byte
        self.status = 0  # status code from server
        self.name_len = ''  # 2 bytes
        self.filename = ''  # without null termination
        # payload fields
        self.size = 0  # 4 bytes
        self.payload = None

    def get_server_info(self):
        """
        retrieves server ip and port from the server.info file
        server.info should look like
         ip:port
        :return:
        """
        fh = open('server.info', 'r')
        info = fh.readline().split(':')
        fh.close()

        if info[0] != '127.0.0.1':
            print('Error in server.info file')
            return
        elif not(info[1].isdecimal()) or not(0 < len(info[1]) < 5):
            print('Error in server.info file')
            return

        self._server_host = info[0]
        self._server_port = int(info[1])
        # print(f'Server info {self._server_host}:{self._server_port}')

    def get_backup_local_files_list(self) -> list:
        """
        returns a list of file name to backup.
        these files need to be saved locally in the client's local directory.
        (this script's directory)
        :return:
        """
        filenames = []
        fh = open('backup.info', 'r')
        while True:
            line = fh.readline()
            if line:
                filenames.append(line.strip())
            else:
                break
        fh.close()
        return filenames

    def connect(self, host, port):
        print(f"Connecting to server {host} {port}")
        self.sock.connect((host, port))

    def close(self):
        # send end session msg: empty payload
        print("Closing connection")
        self.sock.close()

    def recv_response(self):
        """
        receive back the response message from the server
        :return:
        """
        response = self.sock.recv(buffer_size)
        # parse the response message
        self.version = response[0]
        self.status = int.from_bytes(response[1: 3], 'little')

        # if name_len is included - parse it
        if len(response) > 4:
            self.name_len = int.from_bytes(response[3: 5], 'little')

        # if filename and size are included - parse them
        if len(response) > 6:
            self.filename = response[5: 5 + self.name_len]
            self.size = int.from_bytes(response[5 + self.name_len: 9 + self.name_len], 'little')

        print(f"Server's response code: {translate(self.status)}")
        # print(f'response sequence:\n{response}')

    def header(self, operation=0, name_len=0, filename="") -> bytes:
        """
        Construct the header of the message
        :param operation: Operation to send to the server, 1B
        :param name_len: Length of the filename, 2B
        :param filename: The name of the file, variable number of bytes
        :return: Bytes object that represents the header.
        """
        if operation == 0:
            raise ValueError('operation is illegal')

        h = self.userid.to_bytes(4, 'little')
        h += self.version.to_bytes(1, 'little')
        h += operation.to_bytes(1, 'little')
        h += name_len.to_bytes(2, 'little')
        h += filename.encode('utf-8')
        return h

    def send_file(self, fh):
        """
        reads the file content on chunks and sends them till
        the end of the file
        :param fh: file handle
        """
        print('Sending file...')
        chunk = True

        while chunk:
            chunk = fh.read(buffer_size)
            if chunk:
                try:
                    self.sock.sendall(chunk)
                except socket.error as exc:
                    print(f'Socket connection is broken, {exc} , terminating client')
                    return
        print('Done sending')

    def receive_file(self, fh, file_size: int):
        """
        receives chunks of file from server. the chunks will
        be written to the fh file descriptor
        :param file_size: the size of the file to receive
        :param fh: file handle (file descriptor)
        :return: total number of bytes received
        """
        print(f'Attempting to receive file size: {file_size} bytes')
        print('Receiving file...')
        size = 0

        while size <= file_size:
            try:
                chunk = self.sock.recv(buffer_size)
                size += len(chunk)

                if size <= file_size:
                    fh.write(chunk)

                else:
                    # reached the last chunk that contributes more than file_size.
                    # hence, write only the bytes to equalize to file_size
                    fh.write(chunk[0: len(chunk) - (size - file_size)])
                    size -= (size - file_size)
                    break

            except socket.error as exc:
                print(f'Socket connection is broken, {exc} , terminating client')
                return size
        print('Done receiving')
        return size

    def backup_file(self, filename: str):
        """
        backup file request
        :param filename: the file path to backup
        :return:
        """
        print(f"Request to backup file: {filename}")

        try:
            fh = open(filename, 'rb')
        except Exception as exc:
            print(f'Cannot open {filename}, {exc}')
            return

        # make sure the file is not empty
        try:
            chunk = fh.read(buffer_size)
            if not chunk:
                raise RuntimeError
        except RuntimeError:
            print(f'File {filename} is empty. Terminating backup operation')
            fh.close()
            return
        fh.seek(0)  # return the pointer to the begining of the file

        file_size = os.stat(filename).st_size
        if file_size >= (2 ** 32):
            print(f"File size is to large: {file_size} bytes. Aborting request")
            fh.close()
            return
        print(f"Sending file size: {file_size} bytes")

        # construct header
        msg_header = self.header(BACKUP_FILE, name_len=len(filename), filename=filename)
        self.connect(self._server_host, self._server_port)  # connect to server
        self.sock.sendall(msg_header + file_size.to_bytes(4, 'little'))  # send header + size

        sleep(0.1)  # this delay prevents the merge of header and payload
        # it is necessary to separate between the two

        self.send_file(fh)  # send the payload
        fh.close()
        self.recv_response()  # receive a response message from the server and exit
        if self.status != return_codes['BACKUP_FILE_OR_ERASE_FILE_SUCCESS']:
            print(f'Received error status {translate(self.status)}')
        self.sock.shutdown(socket.SHUT_WR)  # notify to server that client has finished sending
        self.close()  # close connection

    def get_file(self, filename: str, dest_name: str):
        """
        get file request
        :param filename: the file to receive from server
        :param dest_name: the name of the file in client's local directory (destination name)
        :return:
        """
        print(f"Request to get file from server: {filename}")
        print(f'Will be saved locally as: {dest_name}')

        try:
            fh = open(dest_name, 'wb')
        except Exception as exc:
            print(f'Cannot open {dest_name}, {exc}')
            return

        # construct header
        msg_header = self.header(GET_FILE, name_len=len(filename), filename=filename)

        self.connect(self._server_host, self._server_port)  # connect to server
        self.sock.sendall(msg_header)  # send header
        self.recv_response()  # receive a response message from the server before receiving file
        if self.status != return_codes['GET_FILE_SUCCESS']:
            print(f'Received error status {translate(self.status)}')
            self.sock.shutdown(socket.SHUT_WR)  # notify to server that client has finished sending
            self.close()  # close connection
            return

        sleep(0.1)  # this delay prevents the merge of header and payload
        # it is necessary to separate between the two

        recv_size = self.receive_file(fh, self.size)  # send the payload
        fh.close()
        print(f'Received file of size: {recv_size} bytes')
        try:
            if recv_size != os.stat(filename).st_size:
                raise RuntimeError
        except RuntimeError:
            print('Warning: Mismatch. The received file size does not equal to size on server')
        self.sock.shutdown(socket.SHUT_WR)  # notify to server that client has finished sending
        self.close()  # close connection

    def erase_file(self, filename: str):
        """
        erase file request
        :param filename: filename to erase in client's backup directory
        :return:
        """
        print(f"Request to erase file: {filename}")
        msg_header = self.header(ERASE_FILE, name_len=len(filename), filename=filename)

        self.connect(self._server_host, self._server_port)  # connect to server
        self.sock.sendall(msg_header)  # send header
        self.recv_response()  # receive a response message from the server before receiving file
        if self.status != return_codes['BACKUP_FILE_OR_ERASE_FILE_SUCCESS']:
            print(f'Received error status {translate(self.status)}')
        self.sock.shutdown(socket.SHUT_WR)  # notify to server that client has finished sending
        self.close()  # close connection

    def get_backup_list(self):
        """
        get list of all backed up files request
        :return:
        """
        print("Request to get backup list")
        msg_header = self.header(GET_BACKUP_LIST)

        self.connect(self._server_host, self._server_port)  # connect to server
        self.sock.sendall(msg_header)  # send header
        self.recv_response()  # get response
        if self.status != return_codes['GET_BACKUP_LIST_SUCCESS']:
            print(f'Received error status {translate(self.status)}')
            self.sock.shutdown(socket.SHUT_WR)
            self.close()
            return

        sleep(0.1)
        print(f'Receiving backup directory list, name={self.filename}, size={self.size}:')
        line = b"_"
        count = 0  # line count

        try:
            while line and (count <= self.size):
                line = self.sock.recv(buffer_size)
                count += 1
                print(line.decode())

        except socket.error as exc:
            print(f'Socket connection is broken, {exc} , terminating client')
        finally:
            self.sock.shutdown(socket.SHUT_WR)
            self.close()


def main():
    # get list of file on server's client directory
    client = MySocket(1234)
    client.get_backup_list()

    # backup the first file
    client = MySocket(1234)
    client.backup_file(client.backup_list[0])

    # backup the second file
    client = MySocket(1234)
    client.backup_file(client.backup_list[1])

    # get list of file on server's client directory
    client = MySocket(1234)
    client.get_backup_list()

    # retrieve from backup the first file, and save it as tmp
    client = MySocket(1234)
    client.get_file(client.backup_list[0], 'tmp')

    # erase first file from backup directory on server
    client = MySocket(1234)
    client.erase_file(client.backup_list[0])

    # retrieve from backup the first file, and save it as tmp2 (expect an error)
    client = MySocket(1234)
    client.get_file(client.backup_list[0], 'tmp2')


if __name__ == "__main__":
    main()
