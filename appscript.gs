function doGet(e) {
  var ss = SpreadsheetApp.openById("1jK49cp18S81G-vTHirE0XXVQMjp6_GK2uUlbW0GagL8");
  var sheet = ss.getActiveSheet();
  
  // Get URL parameters from ESP32
  var slotNum = e?.parameter?.slot || "Unknown"; 
  var event = e?.parameter?.event || "Unknown";   
  
  // Convert slot number to labels
  var slotLabel = "Unknown";
  if (slotNum === "1") slotLabel = "Slot A";
  if (slotNum === "2") slotLabel = "Slot B";
  if (slotNum === "3") slotLabel = "Slot C";
  if (slotNum === "4") slotLabel = "Slot D";

  // Create Headers if the sheet is completely blank
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(["S.N", "Date", "Arrival Time", "Slot Occupied", "Available Slots Count", "Leaving Time"]);
  }

  var now = new Date();
  
  // Force English locale format to prevent Devanagari script output
  var currentDate = now.toLocaleDateString("en-US");
  var currentTime = now.toLocaleTimeString("en-US");
  
  var lastRow = sheet.getLastRow();

  // Helper logic: Get the current available count from the sheet before making changes
  var currentAvailable = 4; // Default starting count if it's the first entry
  if (lastRow > 1) {
    // Look through the last few rows to find where the last count was stored
    for (var r = lastRow; r > 1; r--) {
      var val = parseInt(sheet.getRange(r, 5).getValue());
      if (!isNaN(val)) {
        currentAvailable = val;
        break;
      }
    }
  }

  // ─── LOGIC FOR ENTRY (Arrival) ──────────────────────────────────────────
  if (event === "ENTRY") {
    var nextSN = lastRow; 

    // Car arrives -> Slots decrease by 1
    currentAvailable = currentAvailable - 1;
    if (currentAvailable < 0) currentAvailable = 0; 

    // Clear the Available Slots column for ALL previous rows to keep them vacant
    if (lastRow > 1) {
      sheet.getRange(2, 5, lastRow - 1, 1).clearContent();
    }

    // Append the new arrival row at the bottom with the live count
    sheet.appendRow([
      nextSN,             // Column 1: S.N
      currentDate,        // Column 2: Date
      currentTime,        // Column 3: Arrival Time
      slotLabel,          // Column 4: Slot Occupied
      currentAvailable,   // Column 5: Available Slots Count (Stored only here)
      ""                  // Column 6: Leaving Time
    ]);
  } 
  
  // ─── LOGIC FOR EXIT (Leaving) ───────────────────────────────────────────
  else if (event === "EXIT") {
    var data = sheet.getDataRange().getValues();
    var foundRow = -1;
    
    // Search from the bottom up to find the active parked car row for this slot
    for (var i = data.length - 1; i >= 1; i--) {
      var rowSlot = data[i][3]; 
      var rowExitTime = data[i][5]; 
      
      if (rowSlot === slotLabel && (rowExitTime === "" || rowExitTime === undefined)) {
        foundRow = i + 1; 
        break;
      }
    }
    
    // Car leaves -> Slots increase by 1
    currentAvailable = currentAvailable + 1;
    if (currentAvailable > 4) currentAvailable = 4; 

    // Stamp the leaving time on the car's original row
    if (foundRow !== -1) {
      sheet.getRange(foundRow, 6).setValue(currentTime); 
    }

    // Clear the Available Slots column for ALL rows to ensure no duplicates
    if (lastRow > 1) {
      sheet.getRange(2, 5, lastRow - 1, 1).clearContent();
    }

    // Always force the updated live count onto the very last row's cell
    sheet.getRange(lastRow, 5).setValue(currentAvailable);
  }

  return ContentService.createTextOutput("Success");
}
