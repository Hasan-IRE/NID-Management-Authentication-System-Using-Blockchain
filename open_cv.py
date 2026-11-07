import cv2
import time

# ==========================================
# NID SECUREGUARD - FACE DETECTION
# ESP32-S3 CAM + OV3660 + OpenCV
# ==========================================

# Replace with your ESP32 IP address
ESP32_IP = "10.133.196.87"

STREAM_URL = f"http://{ESP32_IP}/stream"

# Load OpenCV Haar Cascade
face_cascade = cv2.CascadeClassifier(
    cv2.data.haarcascades +
    "haarcascade_frontalface_default.xml"
)

if face_cascade.empty():
    print("Error: Face detection model not loaded!")
    raise SystemExit

# Connect to ESP32 Camera
print("Connecting to ESP32 Camera...")

cap = cv2.VideoCapture(STREAM_URL)

if not cap.isOpened():
    print("Camera connection failed!")
    print("Check ESP32 IP and Wi-Fi connection.")
    raise SystemExit

print("Camera connected successfully!")
print("Face Detection Started!")

while True:

    ret, frame = cap.read()

    if not ret:
        print("Frame capture failed!")
        break

    # Convert frame to grayscale
    gray = cv2.cvtColor(
        frame,
        cv2.COLOR_BGR2GRAY
    )

    # Detect faces
    faces = face_cascade.detectMultiScale(
        gray,
        scaleFactor=1.1,
        minNeighbors=5,
        minSize=(30, 30)
    )

    # Draw bounding boxes
    for (x, y, w, h) in faces:

        cv2.rectangle(
            frame,
            (x, y),
            (x + w, y + h),
            (0, 255, 0),
            2
        )

        cv2.putText(
            frame,
            "Face Detected",
            (x, y - 10),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.7,
            (0, 255, 0),
            2
        )

    # Display detection count
    count = len(faces)

    status = (
        "FACE DETECTED"
        if count > 0
        else "NO FACE DETECTED"
    )

    cv2.putText(
        frame,
        f"Status: {status}",
        (10, 30),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.7,
        (0, 255, 255),
        2
    )

    cv2.putText(
        frame,
        f"Faces: {count}",
        (10, 60),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.7,
        (255, 255, 255),
        2
    )

    # Show video
    cv2.imshow(
        "NID SecureGuard - Face Detection",
        frame
    )

    # Press Q to exit
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

# Release resources
cap.release()
cv2.destroyAllWindows()

print("Face Detection Stopped!")
