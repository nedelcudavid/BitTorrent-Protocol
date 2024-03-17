# BitTorrent Protocol
## Copyright 2024 Nedelcu Andrei-David

### Structures used:
- **FileSwarm**: Represents a swarm of information including file name, size, and list of clients with their owned segments.
- **ClientContentInfo**: Holds information about segments owned by a client in the swarm, including rank and segments.
- **File**: Structure representing a file, containing name, size, and list of hashes.
- **ThreadArg**: Structure used to pass input information to the download thread of each client.

### Peer Logic:
#### (Download & Tracker Communication)
- Extract all input information and send it to the tracker.
- Wait for confirmation message from the tracker indicating initialization completion with all clients, signaling the start of the download phase.
- The download phase iterates through desired files, with each iteration having a file download loop that continues until the file is completely downloaded. 
- During each iteration, a file_swarm_request message is formed ("file_swarm_request <file_name>"), and the current_download_file variable is created to track the progress of the current file download.
- The file download loop follows an efficiency logic explained below.
- After completing each file download session, update messages are sent to the tracker, including the number of segments downloaded in the last session and the index of the first downloaded hash in the last session.
- Check for file completion; if complete, send an update to the tracker, wait for confirmation, and write the output file. If not complete, restart the download loop.
- After completing iterations for desired files, send a message to the tracker indicating completion of download.

#### (Upload)
- The upload thread has a continuous loop to receive messages. If the message is from a client, send back the requested segment ("ok"). If the message is from the tracker, close the thread (as the only message received by the upload thread signifies completion of download for all files). Message source differentiation is done via tags.

### Tracker Logic:
- The tracker maintains a database called files_swarm containing all file swarms. During initialization, it receives input information from all clients, constructing the database. Once it has received input from all clients, it sends messages to all clients, signaling the start of the download phase.
- While clients_downloading_remaining is greater than 0, the tracker continuously receives requests and acts accordingly:
  - file_swarm_request: Checks the current database, finds the requested file, and sends its swarm to the sender.
  - state_update: Updates the database based on received instructions (searches for the file, the client's section in the swarm, and the index specified in the message, and fills in the hash values of the segments downloaded by that client).
  - file_download_complete: Sends a message confirming receipt of notification.
  - all_files_downloaded_successfully: Decrements clients_downloading_remaining by 1.
- When exiting this loop, all clients have finished downloading, so the tracker sends messages to all clients to close the upload thread.
