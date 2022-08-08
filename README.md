# File Transfer Protocol implementation


### **Setup and use locally**

1. Clone the repo and change directory

```
git clone https://github.com/anand-2020/ftp.git
cd ftp
```

2. Compile the `server.cpp` and `client.cpp` files

```
g++ server.cpp -o server -lpthread
g++ client.cpp -o client -lpthread
```

4. Run the server

```
./server <port_no>
```

5. Run the client

```
./client <host_name> <port_no> 
```


### **Commands**

1. Client side

<table>
  <tr>
    <td> <h4>Command</h4> </td>
    <td> <h4>Use</h4> </td>
  </tr>
  <tr>
    <td> GET %filename% </td>
    <td> download file having name %filename% from server </td>
  </tr>
   <tr>
    <td> GET %filename% -b </td>
    <td> download file in binary mode </td>
  </tr>
  <tr>
    <td> PUT %filename% </td>
    <td> upload file having name %filename% to server </td>
  </tr>
   <tr>
    <td> PUT %filename% -b </td>
    <td> upload file in binary mode </td>
  </tr>
  <tr>
    <td> close </td>
    <td> end connection with server </td>
  </tr>
  </table>