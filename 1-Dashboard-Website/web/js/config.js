// SmartLab Guardian - Firebase project settings (web app "SmartLab Dashboard")
// A Firebase web apiKey is a public project identifier, not a password.
// The database is protected by the rules in firebase/database.rules.json.
export const firebaseConfig = {
  apiKey: "AIzaSyDX9TyKuhToAP8oghdEXYiR3bk79lspSko",
  authDomain: "smartlab-guardian-1f880.firebaseapp.com",
  databaseURL: "https://smartlab-guardian-1f880-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId: "smartlab-guardian-1f880",
  storageBucket: "smartlab-guardian-1f880.firebasestorage.app",
  messagingSenderId: "189525623453",
  appId: "1:189525623453:web:a506c35e898c325c2a8a74",
};

// Login accounts (username -> Firebase Auth account). STAFF = full control, HOD = summary view.
// The first successful login creates the account in Firebase Auth.
// The HOD email must match the one in firebase/database.rules.json (only HOD may change settings).
export const USERS = {
  staff: { email: "staff@smartlab.local", password: "Staff@123", role: "STAFF", title: "Lab Staff" },
  hod:   { email: "hod@smartlab.local",   password: "Hod@123",   role: "HOD",   title: "Head of Dept" },
};

export const ROOT = "smartlab";          // every path lives under /smartlab
export const HARDWARE_OFFLINE_MS = 20000;  // ESP32 counted offline after 20 s without data
