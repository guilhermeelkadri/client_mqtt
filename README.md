# ESP-32 Client MQTT

This repository contains the source code and an accompanying article for a project that explores real-time operating systems (RTOS) and their implementation using FreeRTOS. 
The project focuses on demonstrating data exchange in an MQTT network using an ESP32 board with Wi-Fi and Bluetooth modules.


## Summary

The article provides a brief introduction to real-time operating systems and discusses the reasons for choosing FreeRTOS for the project. It explains the general characteristics of an RTOS and highlights the deterministic nature of real-time systems. The application developed for the project demonstrates data exchange in an MQTT network, utilizing the concurrency and deterministic features of RTOS to ensure secure and timely data delivery. The article concludes with an analysis of the expected results and possible application improvements.
Key Concepts Covered

- Real-time operating systems (RTOS) and their characteristics
- Introduction to FreeRTOS and its advantages
- Task scheduling and context switching in RTOS
- Overview of task scheduling algorithms (e.g., Round Robin, Interrupt-based)
- Implementation of FreeRTOS using the ESP32 board
- Communication using the MQTT protocol
- Concurrency and determinism in RTOS
- Analysis of project results and potential improvements

## Source Code

The main.c file contains the source code for the project. It includes various libraries and header files necessary for working with FreeRTOS, ESP32, MQTT, and other components. The code demonstrates tasks for collecting data from GPIO and publishing it to MQTT topics. The ESP32 board is used as a client to connect to a remote MQTT broker, enabling communication between devices.
Usage

To use the source code, follow these steps:

- Set up the ESP32 development environment.
- Copy the main.c file into your project directory.
- Make any necessary adjustments to the code, such as Wi-Fi credentials and MQTT broker details.
- Compile and flash the code onto the ESP32 board.
- Observe the data exchange and MQTT communication on the specified topics.

Please refer to the original article for a more detailed explanation of the code and its functionality.

## Conclusion

This project successfully applies the concepts and functionalities of real-time operating systems using FreeRTOS. It demonstrates the exchange of messages between tasks and the concurrency of tasks in a real-time environment. Additionally, the project validates the usage of the MQTT communication protocol with the provided Espressif API, implemented on the ESP32 board. The article serves as a comprehensive guide to understanding and implementing real-time systems using FreeRTOS and MQTT.