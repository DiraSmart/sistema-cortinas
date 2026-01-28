import serial
import time

port = serial.Serial('COM7', 115200, timeout=1)
print("Conectado a COM7. Esperando datos...")
print("Presiona el boton 'Detectar A-OK' en la web y luego el boton del control remoto")
print("-" * 60)

start = time.time()
while time.time() - start < 120:  # 2 minutos
    if port.in_waiting > 0:
        line = port.readline().decode('utf-8', errors='ignore').strip()
        if line:
            print(line)
    time.sleep(0.01)

port.close()
print("\n" + "-" * 60)
print("Captura terminada")
