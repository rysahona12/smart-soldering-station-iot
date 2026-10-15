/**
 * Smart Soldering Station — Google Sheets logger
 *
 * Deploy as a Web App (Deploy → New deployment → Web app, access: "Anyone")
 * and paste the deployment URL into `google_script_url` in
 * firmware/wemos_d1_mini/wemos_d1_mini.ino.
 *
 * Expected sheet columns:
 *   Timestamp | Temperature (°C) | Fumes | Dock Status | Motion Status
 *
 * Payload from the Wemos D1 Mini (every 10 minutes):
 *   {"t":305, "f":0, "d":1, "m":0}
 *   t = temperature × 10, f = fumes active, d = docked, m = moving
 */
function doPost(e) {
  try {
    var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
    var data = JSON.parse(e.postData.contents);

    var timestamp = new Date();
    var temp = data.t !== undefined ? (data.t / 10.0) : 0;
    var fumes = data.f !== undefined ? (data.f == 1 ? "Fumes Detected" : "Clear") : "Unknown";
    var dock = data.d !== undefined ? (data.d == 1 ? "Docked" : "Undocked") : "Unknown";
    var motion = data.m !== undefined ? (data.m == 1 ? "Moving" : "Idle") : "Unknown";

    sheet.appendRow([timestamp, temp, fumes, dock, motion]);

    return ContentService.createTextOutput(JSON.stringify({"status": "success"}))
                         .setMimeType(ContentService.MimeType.JSON);
  } catch (error) {
    return ContentService.createTextOutput(JSON.stringify({"status": "error", "message": error.toString()}))
                         .setMimeType(ContentService.MimeType.JSON);
  }
}
