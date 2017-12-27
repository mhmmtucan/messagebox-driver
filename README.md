# Project Description

A device driver that will act as a simple message box between the users on a system. Placing a message into the message box will be achieved through a write opertion on the device (named “/dev/messagebox”), starting with a constant prefix such as "@USERNAME" that will specify the recipient. When a user reads from the device, he/she will only see the messages addressed to him/her.

For example, if the user "alice" wants to send a message to the user "bob" to say "Hello", she will write to the device the text "@bob Hello". Later, when user "bob" reads from the device he will see the message as "alice: Hello". Although the message box is global for all users, user "bob" will not see any messages sent to other users.

# Usage

Send messages: `echo "@joe hello" > /dev/messagebox`

Read messages: `cat /dev/messagebox`