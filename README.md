# Tsam8

## COMPILE AND RUN
To compile both the client and the server, you should be in the TSAM SERVER
and to run it you just write make

Running the server
./tsamserver <port>
Example ./tsamserver 4044

Running the client
./tsamserver <server_ip><port>
Example ./tsamclient 127.0.0.1 4044

After type CONNECT,<Group_name>

## Operating System
This code was implemented for both Linux and Mac using poll()

## Commands Implemented
Client to Server: CONNECT,SENDMSG,GETMSG
Server to Server: HELO,SERVERS,SENDMSG,KEEPALIVE,STATUSREQ,STATUSRESP,GETMSGS,ERROR

## Server Behaviour
Several Points are to be said about our server behaviour

(Patience): if a message arrives for a group that our is server is not connect we dont discard it immediately, but we save it temporary until a route or a connection to that group becomes available, then we forward the store message/s

(Message Relaying): if we get a message intended for another group, we act like a middle man and if we have connection we send it straight away otherwise we store temporarily until a direct connection opens up

(Automatic Peer Discovery): We connect to other servers based on the SERVERS list and automatically try to establish connections with newly discovered groups of course up to the limit ammount of connections

(Periodic Keepalives): Every 60s the server send a meesage to all the serves connected to ensure the we are connected and to let them know the amount of store messages we have for them

(Status Polling and Retrieval): Every 60s the server sends a STATUSREQ to check what messages peer servers are holding for other groups and after receiving STATUSRESP if we find that another server is holding messages for us or a directly connected peer, it automatically triggers a GETMSGS to fetch them

(Connection Limits and Safeguards): The server enforces strict connection to prevent system overload while reserving free slots to ensure new clients or peer can always connect succesfully

(TSAM server connection): When we start the server automatically tries to connect to the Instructor server(TSAM server) to complete the helo and get the servers list to start connecting to other peers 

## Point and Bonus Points

## 1(c)
Our server successfully received messages addressed to our group and a client fetches it
Evidence:

Group     | incoming line | client delivery line | timestamp
G_uinnes  | 56362         | 56384                | 2026-11-02T20:37:56Z
G_bahh    | 56676         | 56801                | 56676:2026-11-02T20:47:37Z

## 1(d)
Our server successfully forwarded a message to a directly connected group
Evidence:

Group: G_G-Monster, Lines: 43615-43626, timestamp: 2026-11-02T12:27:02Z 
Group: G_unit, Lines: 43689 - 43690, timestamp: 2026-11-02T12:29:01Z

## Ready bot
```
Our server was connected on the botnet and responding for at least 2 hours on these given timestamps:
2026-10-29T20:51:24Z
2026-10-30T20:20:14Z
2026-10-31T20:45:31Z
2026-11-01T16:47:09Z
2026-11-02T12:57:58Z
```
## Indirect bot
### Relaying 
Our server relayed at least 10 messages between other groups, where we were neither the sender or recipient. The following log lines pairs show each message arriving and and being forwarded to the correct recipient.
```
from            | to            | incoming line | outgoing line | timestamp
-----------------------------------------------------------------------
I_oracle        |G_strawhats    | 12818        | 13333          | 2026-10-30T21:02:31Z RECV 2026-10-30T21:18:02Z SEND
 
I_oracle        |G_G            | 13878        | 13928          | 2026-10-30T21:33:31Z RECV 2026-10-30T21:35:11Z SEND

G_nafn          |G_nammi        | 23111        | 23588          | 2026-10-31T20:46:29Z RECV 2026-10-31T20:50:42Z SEND

I_dispatch      |G_kleina       | 43475        | 43476          | 2026-11-02T12:22:59Z RECV 2026-11-02T12:22:59Z SEND

I_dispatch      |G_kleina       | 43477        | 43478          | 2026-11-02T12:22:59Z RECV 2026-11-02T12:22:59Z SEND

G_G-Monster     |G_unit         | 43642        | 43643          | 2026-11-02T12:27:19Z RECV 2026-11-02T12:27:19Z SEND

G_G-Monster     |G_unit         | 43844        | 43845          | 2026-11-02T12:30:39Z RECV 2026-11-02T12:30:39Z SEND

G_G-Monster     |G_unit         | 43940        | 43941          | 2026-11-02T12:33:39Z RECV 2026-11-02T12:33:39Z SEND

G_Monster       |G_bongo-boiz   | 48058        | 48059          | 2026-11-02T13:51:59Z RECV 2026-11-02T13:51:59Z SEND

G_stammtischen  |I_dispatch     | 51691        | 51692          | 2026-11-02T15:24:59Z RECV 2026-11-02T15:24:59Z SEND
```
### Patience
Our server stores messages for a group that is unreachable. When the group becomes available our server forwards the original message.
Two examples given demonstrating this behaviour:

1.
Line 12818  2026-10-30T21:02:31Z RECV: Our server received a message from I_oracle to G_strawhats.
Line 12819  2026-10-30T21:02:31Z note: Our server records that there is no route available and stores the message.
Line 13333  2026-10-30T21:18:02Z SEND: Our server forwards to stored message to G_strawhats when it is available again.

2.
Line 13878  2026-10-30T21:33:31Z RECV: Our server received a message from I_oracle to G_G.
Line 13879  2026-10-30T21:33:31Z note: Our server records that there is no route available and stores the message.
Line 13928  2026-10-30T21:35:11Z SEND: Our server forwards to stored message to G_G when it is available again.



## Edge bot
Our server maintained connections with at least three non instructor peers on five seperate occasions, each more than five hours apart.
Evidence:
```
| connected                     | HELO line            | SERVER line | timestamp
----------------------------------------------------------------------------
G_swaws, G_portal, G_apple      | 2293, 2297, 2302     | 2303        | 2026-10-28T12:24:57Z

G_swaws, G_G-Monster, G_apple   | 2645, 2659, 2661     | 2662        | 2026-10-28T17:45:44Z

G_TangoLango, G_hearts, G_kleina | 40343, 40289, 40183 | 40345       | 2026-11-01T16:51:11Z 

G_force, G_brb, G_bongo-boiz     | 43219, 43238, 43252 | 43253       | 2026-11-01T20:04:08Z

G_G-Monster, G_unit, G_BD        | 43748, 44477, 44653 | 44655       | 2026-11-02T12:54:45Z 
```

## Courier bot
Our server completed 2 dispatches, they were both credited.
Evidence in server log:

Dispatch 0:
Reply: 
11356 - 11357, timestamp: 2026-10-30T20:14:24Z
Credit:
11358 timestamp: 2026-10-30T20:14:24Z

Dispatch 3:
Reply:
43281 - 43282, timestamp: 2026-11-01T20:05:03Z
Credit:
Line: 43593 timestamp:2026-11-02T12:26:20Z 


## Cartographer Bot
### Map 1 2026-10-29T15:25:09Z
```
G_GOATS: [I_instr1, I_instr2, G_swaws, G_christnik, G_kleina, G_portal, G_apple]
I_instr1: [G_swaws, G_christnik, I_instr2, G_kleina]
I_instr2: [I_dispatch, I_tester, I_registrar, I_gremlin, I_oracle, G_christnik, I_instr1]
G_swaws: [G_portal, I_instr1, G_apple, G_blueberries, G_mjolk, G_null, G_bachata, G_leo, G_G-Monster, G_christnik, G_wow, G_order66]
G_christnik: [I_instr1,G_swaws,I_instr2,G_kleina,G_portal,G_apple,G_blueberries,G_mjolk,G_idiosyncrasy,G_null,G_bachata,G_leo,G_G-Monster,G_wow,I_dispatch,I_tester,I_registrar,I_gremlin,I_oracle,G_magnum,G_nafn,G_G,G_nocco,G_SYN-Sational,G_minions,G_force,G_ASA,G_ellur,G_kiwi,G_order66]
G_kleina: [I_gremlin, I_instr1, G_christnik, G_bachata, G_blueberries]
G_portal: []
G_apple: [G_magnum, G_swaws, G_G-Monster, G_portal, G_mjolk, G_null, G_blueberries, G_leo, G_christnik]
```
Evidence in server.log: lines 3254-3291. Self entries and duplicates removed

### Map 2 2026-10-30T20:03:53Z
```
G_GOATS: [I_instr1, G_strawhats, G_TangoLango, G_kleina, G_portal, G_apple, G_EastBot, G_wow]
I_instr1: [G_christnik, G_strawhats, I_instr2, G_kleina, G_TangoLango]
G_strawhats: []
G_TangoLango: [I_instr1, G_christnik, G_strawhats, I_instr2, G_kleina, G_portal, G_apple, I_dispatch]
G_portal: [G_EastBot, G_mjolk, G_idiosyncrasy, G_christnik, G_TangoLango, G_leo, G_nafn]
G_apple: [G_wow, G_magnum, G_G-Monster, G_mjolk, G_TangoLango, G_order66, G_lillefre, G_blueberries, G_christnik, G_NettNet, G_leo]
G_EastBot: [G_NettNet, G_christnik, G_stammtischen, G_TangoLango, G_lillefre, G_portal, G_null, G_mjolk, G_preacherclub, G_idiosyncrasy, G_yourname]
G_kleina: [I_instr1, I_gremlin, G_nafn, G_TangoLango]
G_wow: [G_apple, G_christnik, G_everton, G_idiosyncrasy]
```
Evidence in server.log: lines 10821-10900. Self entries and duplicates removed


### Map 3 2026-10-31T20:45:31Z 
```
G_GOATS: [I_instr1, G_strawhats, I_instr2, G_kleina, I_dispatch, I_tester, I_registrar]
I_instr1: [G_strawhats, G_christnik, I_instr2, G_kleina]
G_strawhats: []
I_instr2: [I_dispatch, I_tester, I_registrar, I_gremlin, I_oracle, I_instr1, G_christnik]
G_kleina: [I_instr1, G_nafn, G_christnik, I_gremlin]
I_dispatch: [I_instr2, I_fenwick, I_courier, G_christnik]
I_registrar: [I_instr2, G_christnik]
I_tester: [I_instr2, G_christnik, G_hearts, G_ASA]
```
Evidence in server.log: lines 23034-23080. Self entries and duplicates removed
### Observations
Some servers appeared in all three maps, including I_instr1, I_instr2, G_kleina, G_christnik. Visible changes between the maps were that the first two maps included more student groups but the last map it was more instructures in the map. G_christnik advertised a large list of servers that also contained duplicates, while this helps advertise it's not reliable because of the duplicates. I_instr2 seemed to be an important hub since it gave a connection to the other instructure servers, for example I_dispatch, I_tester, I_registrar, I_gremlin. In the third map G_apple and G_EastBot also advertised many connections to groups. We did not find clear evidence that the botnet was split into seperate unreachable parts.

## Authorship
Server implementation: Sylvia
Client implementation: Jeremias
Logging, testing and evidence gathering: Sylvia

Tools used:
AI assistant for code debugging and structuring the code and organizing