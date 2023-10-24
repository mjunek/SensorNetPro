let ajaxBase = "/json-api";
let currentSection;
let statusRefreshTimer;
let config;
const sectionFields = new Object();
sectionFields.admin = ["adminUser"];
sectionFields.network = [
  "networkHostname",
  "networkWifiEnabled",
  "networkDhcpEnabled",
  "networkWifiApMode",
  "networkIP",
  "networkMask",
  "networkGateway",
  "networkPriDNS",
  "networkSecDNS",
  "networkSSID",
];
sectionFields.snmp = ["snmpLocation", "snmpSysName", "snmpContact"];

let sensorTable = $("#sensorTable").DataTable({
  autoWidth: false,
  lengthChange: false,
  info: false,
  processing: false,
  searching: false,
  paging: false,
  ordering: false,
  serverSide: true,
  serverMethod: "POST",
  order: [],
  ajax: {
    url: ajaxBase,
    contentType: "application/json",
    type: "POST",
    timeout: 10000,
    dataType: "json",
    dataSrc: "sensorData",
    data: function (d) {
      d.webAction = "sensor-data";
      return JSON.stringify(d);
    },
  },
  columns: [
    { data: "id" },
    { data: "address" },
    { data: "name" },
    { data: "reading" },
  ],
  columnDefs: [
    {
      targets: 3,
      render: function (data, type, row, meta) {
        return data.toFixed(2) + " ºC";
      },
    },
  ],
});

let sensorConfigTable = $("#sensorConfigTable").DataTable({
  autoWidth: false,
  lengthChange: false,
  info: false,
  processing: false,
  searching: false,
  paging: false,
  ordering: false,
  serverSide: true,
  serverMethod: "POST",
  order: [],
  ajax: {
    url: ajaxBase,
    contentType: "application/json",
    type: "POST",
    dataType: "json",
    dataSrc: "sensorData",
    timeout: 10000,
    data: function (d) {
      d.webAction = "sensor-data";
      return JSON.stringify(d);
    },
  },
  columns: [{ data: "id" }, { data: "address" }, { data: "name" }],
  columnDefs: [
    { targets: 0, data: "id", visible: false },
    {
      targets: 1,
      data: "address",
      render: function (data, type, row, meta) {
        return (
          '<label for="sensorName' +
          row.id +
          '" class="col-form-label">' +
          data +
          "</label>"
        );
      },
    },
    {
      targets: 2,
      data: "name",
      render: function (data, type, row, meta) {
        return (
          '<input type="text" id="sensorName' +
          row.id +
          '" name="sensorName' +
          row.id +
          '" value="' +
          data +
          '" maxlength="35" class="form-control text-form-input"/>'
        );
      },
    },
  ],
});

function updateSystemInfo() {
  let reqData = new Object();
  reqData.webAction = "status";

  $.ajax({
    url: ajaxBase,
    contentType: "application/json",
    type: "POST",
    dataType: "json",
    timeout: 10000,
    data: JSON.stringify(reqData),
    success: function (responseData) {
      $("#hostname_status_text").text(responseData.hostname);
      $("#version_text").text(responseData.version);
      $("#uptime_text").text(responseData.uptime);

      $("#dhcp_text").text(responseData.dhcpEnabled ? "Enabled" : "Disabled");
      if (responseData.dhcpEnabled) {
        $("#dhcp_icon").removeClass("text-muted").addClass("text-success");
        $("#dhcp_text").removeClass("text-muted").addClass("text-success");
        $("#dhcp_icon").html('<use xlink:href="#ico_yes"></use>');
      } else {
        $("#dhcp_icon").addClass("text-muted").removeClass("text-success");
        $("#dhcp_text").addClass("text-muted").removeClass("text-success");
        $("#dhcp_icon").html('<use xlink:href="#ico_no"></use>');
      }
      $("#eth_status_text").text(
        responseData.ethStatus
          ? "Connected | IP: " +
              responseData.ethIp +
              " | Speed: " +
              responseData.etherSpeed
          : "Disconnected"
      );

      if (responseData.ethStatus) {
        $("#eth_status_icon")
          .removeClass("text-danger")
          .addClass("text-success")
          .removeClass("text-muted");
        $("#eth_status_text")
          .removeClass("text-danger")
          .addClass("text-success")
          .removeClass("text-muted");
      } else {
        $("#eth_status_icon")
          .addClass("text-danger")
          .removeClass("text-success")
          .removeClass("text-muted");
        $("#eth_status_text")
          .addClass("text-danger")
          .removeClass("text-success")
          .removeClass("text-muted");
      }

      if (!responseData.wifiEnabled) {
        $("#wifi_status_icon").html('<use xlink:href="#ico_wifi_off"></use>');
        $("#wifi_status_text").text("Disabled");
      } else if (responseData.wifiApMode) {
        $("#wifi_status_icon").html('<use xlink:href="#ico_ap_status"></use>');
        $("#wifi_status_text").text(
          responseData.wifiStatus ? "Client Connected" : "No Client Connected"
        );
      } else {
        $("#wifi_status_icon").html(
          '<use xlink:href="#ico_wifi_status"></use>'
        );
        $("#wifi_status_text").text(
          responseData.wifiStatus
            ? "Associated | IP: " +
                responseData.wifiIp +
                " | RSSI: " +
                responseData.RSSI
            : "Not connected to AP"
        );
      }

      if (responseData.wifiStatus && responseData.wifiEnabled) {
        $("#wifi_status_icon")
          .removeClass("text-danger")
          .addClass("text-success")
          .removeClass("text-muted");
        $("#wifi_status_text")
          .removeClass("text-danger")
          .addClass("text-success")
          .removeClass("text-muted");
      } else if (!responseData.wifiStatus && responseData.wifiEnabled) {
        $("#wifi_status_icon")
          .addClass("text-danger")
          .removeClass("text-success")
          .removeClass("text-muted");
        $("#wifi_status_text")
          .addClass("text-danger")
          .removeClass("text-success")
          .removeClass("text-muted");
      } else {
        $("#wifi_status_icon")
          .removeClass("text-danger")
          .removeClass("text-success")
          .addClass("text-muted");
        $("#wifi_status_text")
          .removeClass("text-danger")
          .removeClass("text-success")
          .addClass("text-muted");
      }
    },
    error: function (error) {
      clearInterval(statusRefreshTimer);
      $("#jsErrorText").text(
        "There was an error refreshing the status information."
      );
      $("#jsError").show();
      setTimeout(() => {
        $("#jsError").hide();
      }, 5000);
    },
  });
}

function showSection(sectionName) {
  clearInterval(statusRefreshTimer);

  switch (sectionName) {
    case "info":
      sensorTable.draw();
      updateSystemInfo();
      statusRefreshTimer = setInterval(() => {
        sensorTable.draw();
        updateSystemInfo();
      }, 10000);
      break;
  }

  let panelToHide = "#" + currentSection + "Panel";
  let panelToShow = "#" + sectionName + "Panel";
  $(panelToHide).hide();
  $(panelToShow).show();
  currentSection = sectionName;
}

function rebootUnit() {
  if (confirm("Are you sure you wish to reboot?")) {
    window.location.replace("reboot.html");
  }
}

function setDhcpStatus(status) {
  $("#networkIP").attr("readonly", status);
  $("#networkMask").attr("readonly", status);
  $("#networkGateway").attr("readonly", status);
  $("#networkPriDNS").attr("readonly", status);
  $("#networkSecDNS").attr("readonly", status);
  validate("network");
}

function setWifiStatus(status) {
  $("#networkWifiModeAp").attr("disabled", !status);
  $("#networkWifiModeSta").attr("disabled", !status);
  $("#networkSSID").attr("readonly", !status);
  $("#networkWpaKey").attr("readonly", !status);
  validate("network");
}

function saveSection(section) {
  console.log("savesection", section);
  if (!validate(section)) return;
  let reqData = new Object();

  switch (section) {
    case "admin":
      reqData.webAction = "save-admin";
      reqData.adminUser = $("#adminUser").val();
      reqData.adminPassword = $("#adminPassword").val();
      reqData.modifyAdminPass = $("#modifyAdminPass").val() == "on";
      break;
    case "network":
      reqData.webAction = "save-network";
      break;
    case "snmp":
      reqData.webAction = "save-snmp";
      reqData.snmpLocation = $("#snmpLocation").val();
      reqData.snmpSysName = $("#snmpSysName ").val();
      reqData.snmpContact = $("#snmpContact").val();
      reqData.snmpCommunity = $("#snmpCommunity").val();
      reqData.modifySnmpCommunity = $("#modifySnmpCommunity").val() == "on";
      break;
    case "sensor":
      reqData.webAction = "save-sensor";
      break;
  }
  console.log("sending ajax req", reqData);
  $.ajax({
    url: ajaxBase,
    contentType: "application/json",
    type: "POST",
    dataType: "json",
    timeout: 10000,
    data: JSON.stringify(reqData),
    success: function (responseData) {
      console.log("success", responseData);
      if (responseData.error == false) {
        for (field in sectionFields[section]) {
          console.log("processing section field", section, field);
        }
        // config = responseData;
        // resetSection(section, true);
        $("#jsSuccessText").text("Configuration saved successfully.");
        $("#jsSuccess").show();
        setTimeout(() => {
          $("#jsSuccess").hide();
        }, 5000);
      } else {
        $("#jsErrorText").text("There was an error saving the configuration.");
        $("#jsError").show();
        setTimeout(() => {
          $("#jsError").hide();
        }, 5000);
      }
    },
    error: function (error) {
      $("#jsErrorText").text("There was an error saving the configuration.");
      $("#jsError").show();
      setTimeout(() => {
        $("#jsError").hide();
      }, 5000);
    },
  });
}

function validate(section) {
  switch (section) {
    case "admin":
      let adminUser = $("#adminUser").val();
      let adminPassword = $("#adminPassword").val();
      let modifyAdminPass = $("#modifyAdminPass").val() == "on";
      if (adminUser == null || adminUser == "") {
        $("#adminUser").addClass("is-invalid").removeClass("is-valid");
        return false;
      } else {
        $("#adminUser").removeClass("is-invalid").addClass("is-valid");
      }

      if (
        !(adminPassword == null || adminPassword == "") &&
        adminPassword.length < 8 &&
        modifyAdminPass
      ) {
        $("#adminPassword").addClass("is-invalid").removeClass("is-valid");
        return false;
      } else if (!modifyAdminPass) {
        $("#adminPassword").removeClass("is-invalid").removeClass("is-valid");
      } else {
        $("#adminPassword").removeClass("is-invalid").addClass("is-valid");
      }
      return true;

    case "network":
      let ipResult = validateIpSettings();
      let wifiResult = validateWifiSettings();
      return ipResult && wifiResult;

    case "snmp":
      let snmpSuccess=true;
      let snmpCommunity = $("#snmpCommunity").val();
      let snmpCommunityConfirm = $("#snmpCommunityConfirm").val();
      let modifySnmpCommunity = $("#modifyCommunity").val() == "on";

      if (
        modifySnmpCommunity &&
        (snmpCommunity == null || snmpCommunity == "")
      ) {
        $("#snmpCommunity").addClass("is-invalid").removeClass("is-valid");
        $("#snmpCommunityValidateError").text(
          "Community String must be supplied"
        );
        snmpSuccess= false;
      } else if (!modifySnmpCommunity) {
        $("#snmpCommunity").removeClass("is-invalid").removeClass("is-valid");
        $("#snmpCommunityConfirm")
          .removeClass("is-invalid")
          .removeClass("is-valid");
        $("#snmpCommunityValidateError").text("");
      } else {

        $("#snmpCommunity").removeClass("is-invalid").addClass("is-valid");

        if (snmpCommunityConfirm == null || snmpCommunityConfirm == "") {
          $("#snmpCommunityConfirm")
            .addClass("is-invalid")
            .removeClass("is-valid");
          $("#snmpCommunityValidateError").text(
            "Please confirm the Community String"
          );
          snmpSuccess= false;
        } else if (snmpCommunityConfirm != snmpCommunity) {
          $("#snmpCommunityConfirm")
            .addClass("is-invalid")
            .removeClass("is-valid");
          $("#snmpCommunityValidateError").text(
            "Community Strings do not match."
          );
          snmpSuccess= false;
        } else {
          $("#snmpCommunityConfirm")
            .removeClass("is-invalid")
            .addClass("is-valid");
        }
      }
      return snmpSuccess;
    case "sensor":
      break;
  }
}

function isValidIP(ipAddress) {
  const ipArray = ipAddress.split(".");
  if (ipArray.length != 4) return false;

  for (i = 0; i < 4; i++) {
    let ipOctet = parseInt(ipArray[i]);
    if (isNaN(ipOctet) || ipOctet < 0 || ipOctet > 255) return false;
  }
  if (parseInt(ipArray[0]) < 1) return false;
  return true;
}

function isValidSubnetMask(subnet) {
  const ipArray = subnet.split(".");
  if (ipArray.length != 4) return false;
  let endFound = false;
  for (i = 0; i < 4; i++) {
    let ipOctet = parseInt(ipArray[i]);
    if (isNaN(ipOctet) || ipOctet < 0 || ipOctet > 255) return false;
    if (ipOctet == 0 && i == 0) return false;
    if (ipOctet == 255) continue;
    if (endFound && ipOctet != 0) return false;
    if (
      ipOctet == 0 ||
      ipOctet == 128 ||
      ipOctet == 192 ||
      ipOctet == 224 ||
      ipOctet == 240 ||
      ipOctet == 248 ||
      ipOctet == 252 ||
      ipOctet == 254
    ) {
      endFound = true;
      continue;
    } else {
      return false;
    }
  }
  return true;
}

function isValidIpGatewaySubnet(ipAddress, subnet, gateway) {
  const ipArray = ipAddress.split(".");
  const subnetArray = subnet.split(".");
  const gatewayArray = gateway.split(".");

  let useGateway = true;
  if (gateway == null || gateway == "" || gatewayArray.length != 4)
    useGateway = false;

  let ipNumber =
    parseInt(ipArray[0]) * 16777216 +
    parseInt(ipArray[1]) * 65536 +
    parseInt(ipArray[2]) * 256 +
    parseInt(ipArray[3]);
  let subnetNumber =
    parseInt(subnetArray[0]) * 16777216 +
    parseInt(subnetArray[1]) * 65536 +
    parseInt(subnetArray[2]) * 256 +
    parseInt(subnetArray[3]);
  let gatewayNumber =
    parseInt(gatewayArray[0]) * 16777216 +
    parseInt(gatewayArray[1]) * 65536 +
    parseInt(gatewayArray[2]) * 256 +
    parseInt(gatewayArray[3]);

  ipNumber = ipNumber & 0xffffffff;
  subnetNumber = subnetNumber & 0xffffffff;
  gatewayNumber = gatewayNumber & 0xffffffff;

  let ipSubnetTest = ipNumber & subnetNumber;
  ipSubnetTest = ipSubnetTest & 0xffffffff;

  let gatewayTest = gatewayNumber & subnetNumber;
  gatewayTest = gatewayTest & 0xffffffff;

  let hostMask = ~subnetNumber;
  hostMask = hostMask & 0xffffffff;

  let broadcastNumber = ipNumber | hostMask;
  broadcastNumber = broadcastNumber & 0xffffffff;

  if (ipNumber == ipSubnetTest) return 1;
  if (ipNumber == broadcastNumber) return 2;
  if (useGateway && gatewayNumber == gatewayTest) return 3;
  if (useGateway && gatewayNumber == broadcastNumber) return 4;
  if (useGateway && ipSubnetTest != gatewayTest) return 5;
  return 0;
}

function validateIpSettings() {
  let dhcpMode = $("input[type='radio'][name='networkDHCP']:checked");
  if (dhcpMode.length > 0) {
    dhcpModeValue = dhcpMode.val();
  }

  if (dhcpModeValue == 1) {
    $("#networkIP").removeClass("is-invalid").removeClass("is-valid");
    $("#networkMask").removeClass("is-invalid").removeClass("is-valid");
    $("#networkGateway").removeClass("is-invalid").removeClass("is-valid");
    $("#networkPriDNS").removeClass("is-invalid").removeClass("is-valid");
    $("#networkSecDNS").removeClass("is-invalid").removeClass("is-valid");
    return true;
  }

  let ipAddress = $("#networkIP").val();
  let subnetMask = $("#networkMask").val();
  let gateway = $("#networkGateway").val();
  let priDns = $("#networkPriDNS").val();
  let secDns = $("#networkSecDNS").val();
  let invalid = false;
  if (!isValidIP(ipAddress)) {
    invalid = invalid | true;
    $("#networkIP").addClass("is-invalid").removeClass("is-valid");
    $("#networkIPValidateError").text("Invalid IP Address");
  } else {
    $("#networkIP").removeClass("is-invalid").addClass("is-valid");
  }

  if (!isValidSubnetMask(subnetMask)) {
    invalid = invalid | true;
    $("#networkMask").addClass("is-invalid").removeClass("is-valid");
    $("#networkIPValidateError").text("Invalid Subnet Mask");
  } else {
    $("#networkMask").removeClass("is-invalid").addClass("is-valid");
  }

  if (gateway != null && gateway != "" && !isValidIP(gateway)) {
    invalid = invalid | true;
    $("#networkGateway").addClass("is-invalid").removeClass("is-valid");
    $("#networkIPValidateError").text("Invalid Gateway");
  } else {
    $("#networkGateway").removeClass("is-invalid").addClass("is-valid");
  }

  if (!isValidIP(priDns)) {
    invalid = invalid | true;
    $("#networkPriDNS").addClass("is-invalid").removeClass("is-valid");
  } else {
    $("#networkPriDNS").removeClass("is-invalid").addClass("is-valid");
  }

  if (!isValidIP(secDns)) {
    invalid = invalid | true;
    $("#networkSecDNS").addClass("is-invalid").removeClass("is-valid");
  } else {
    $("#networkSecDNS").removeClass("is-invalid").addClass("is-valid");
  }
  if (invalid) return false;

  switch (isValidIpGatewaySubnet(ipAddress, subnetMask, gateway)) {
    case 0: //good
      break;
    case 1: // ip = subnet address
      $("#networkIP").addClass("is-invalid").removeClass("is-valid");
      $("#networkIPValidateError").text(
        "IP Address cannot be the subnet address"
      );
      return false;
    case 2: // ip = broadcast
      $("#networkIP").addClass("is-invalid").removeClass("is-valid");
      $("#networkIPValidateError").text(
        "IP Address cannot be the broadcast address"
      );
      return false;
    case 3: //gateway = subnet
      $("#networkGateway").addClass("is-invalid").removeClass("is-valid");
      $("#networkGatewayValidateError").text(
        "Gateway address cannot be the subnet address"
      );
      return false;
    case 4: // gateway = broadcastNumber
      $("#networkGateway").addClass("is-invalid").removeClass("is-valid");
      $("#networkGatewayValidateError").text(
        "Gateway address be the broadcast address"
      );
      return false;
    case 5: //ip and gateway of diff subnets
      $("#networkGateway").addClass("is-invalid").removeClass("is-valid");
      $("#networkGatewayValidateError").text(
        "Gateway address must be on the same subnet as the IP address"
      );
      return false;
  }
  return true;
}

function validateWifiSettings() {
  let wifiMode = $("input[type='radio'][name='networkWifi']:checked");
  if (wifiMode.length > 0) {
    wifiModeValue = wifiMode.val();
  }

  if (wifiModeValue == 0) {
    $("#networkSSID").removeClass("is-invalid").removeClass("is-valid");
    $("#networkWpaKey").removeClass("is-invalid").removeClass("is-valid");
    return true;
  }

  let ssid = $("#networkSSID").val();
  let wpakey = $("#networkWpaKey").val();
  let changeWpaKey = $("#modifyWpaKey").is(":checked");

  if (ssid == null || ssid == "") {
    $("#networkSSID").addClass("is-invalid").removeClass("is-valid");
    return false;
  } else {
    $("#networkSSID").removeClass("is-invalid").addClass("is-valid");
  }

  if (!changeWpaKey) {
    $("#networkWpaKey").removeClass("is-invalid").removeClass("is-valid");
  } else if (wpakey == null || wpakey == "") {
    $("#networkWpaKeyValidateSuccess").text(
      "Notice: No Wi-Fi encryption will be enabled!"
    );
    $("#networkWpaKey").removeClass("is-invalid").addClass("is-valid");
  } else if (wpakey.length < 8) {
    $("#networkWpaKey").addClass("is-invalid").removeClass("is-valid");
    $("#networkWpaKeyValidateError").text(
      "WPA Key must be at least 8 characters"
    );

    return false;
  } else {
    $("#networkWpaKeyValidateSuccess").text("");
    $("#networkWpaKey").removeClass("is-invalid").addClass("is-valid");
  }
  return true;
}

function resetSection(section, force) {
  if (
    !(
      force ||
      confirm("Are you sure you wish to reset all changes on this tab?")
    )
  ) {
    return;
  }
  switch (section) {
    case "network":
      $("#networkHostname").val(config.networkHostname);

      $("#networkSSID").val(config.networkSSID);
      $("#networkWpaKey").val("");
      $("#networkWpaKey").removeClass("is-invalid").removeClass("is-valid");
      $("#networkWpaKey").prop("disabled", true);
      $("#modifyWpaKey").prop("checked", false);

      $("#networkIP").val(config.networkIP);
      $("#networkMask").val(config.networkMask);
      $("#networkGateway").val(config.networkGateway);
      $("#networkPriDNS").val(config.networkPriDNS);
      $("#networkSecDNS").val(config.networkSecDNS);

      $("#networkWifiModeAP").prop("checked", config.networkWifiApMode);
      $("#networkWifiModeSta").prop("checked", !config.networkWifiApMode);

      $("#networkDHCPYes").prop("checked", config.networkDhcpEnabled);
      $("#networkDHCPNo").prop("checked", !config.networkDhcpEnabled);
      setDhcpStatus(config.networkDhcpEnabled);

      $("#networkWifiYes").prop("checked", config.networkWifiEnabled);
      $("#networkWifiNo").prop("checked", !config.networkWifiEnabled);
      setWifiStatus(config.networkWifiEnabled);
      break;

    case "snmp":
      $("#snmpCommunity").val("");
      $("#snmpLocation").val(config.snmpLocation);
      $("#snmpSysName").val(config.snmpSysName);
      $("#snmpContact").val(config.snmpContact);
      $("#snmpCommunity").removeClass("is-invalid").removeClass("is-valid");
      $("#snmpCommunity").prop("disabled", true);
      $("#modifyCommunity").prop("checked", false);
      break;

    case "admin":
      $("#adminUser").val(config.adminUser);
      $("#adminPassword").val("");
      $("#adminPassword").removeClass("is-invalid").removeClass("is-valid");
      $("#adminPassword").prop("disabled", true);
      $("#modifyAdminPass").prop("checked", false);
      break;
    case "sensor":
      sensorConfigTable.ajax.reload();
  }
  validate(section);
}

function getConfig() {
  let reqData = new Object();
  reqData.webAction = "config";

  $.ajax({
    url: ajaxBase,
    contentType: "application/json",
    type: "POST",
    dataType: "json",
    timeout: 10000,
    data: JSON.stringify(reqData),
    success: function (responseData) {
      if (responseData.error == false) {
        config = responseData;
        resetSection("admin", true);
        resetSection("snmp", true);
        resetSection("network", true);
      } else {
        $("#jsErrorText").text("There was an error getting the configuration.");
        $("#jsError").show();
        setTimeout(() => {
          $("#jsError").hide();
        }, 5000);
      }
    },
    error: function (error) {
      $("#jsErrorText").text("There was an error getting the configuration.");
      $("#jsError").show();
      setTimeout(() => {
        $("#jsError").hide();
      }, 5000);
    },
  });
}

function setModifyAdminPass(checkbox) {
  if (!checkbox.checked) {
    $("#adminPassword").val("");
    $("#adminPassword").removeClass("is-invalid").removeClass("is-valid");
  }
  $("#adminPassword").prop("disabled", !checkbox.checked);
}

function setModifyWpaKey(checkbox) {
  if (!checkbox.checked) {
    $("#networkWpaKey").val("");
    $("#networkWpaKey").removeClass("is-invalid").removeClass("is-valid");
  }
  $("#networkWpaKey").prop("disabled", !checkbox.checked);
}

function setModifyCommunity(checkbox) {
  if (!checkbox.checked) {
    $("#snmpCommunity").val("");
    $("#snmpCommunity").removeClass("is-invalid").removeClass("is-valid");
    $("#snmpCommunityConfirm").val("");
    $("#snmpCommunityConfirm")
      .removeClass("is-invalid")
      .removeClass("is-valid");
  }
  $("#snmpCommunity").prop("disabled", !checkbox.checked);
  $("#snmpCommunityConfirm").prop("disabled", !checkbox.checked);
}

function factoryReset() {
  if (
    confirm(
      "Are you sure you wish to reset all settings to their factory defaults?"
    )
  ) {
    let reqData = new Object();
    reqData.webAction = "factory-reset";

    $.ajax({
      url: ajaxBase,
      contentType: "application/json",
      type: "POST",
      dataType: "json",
      timeout: 10000,
      data: JSON.stringify(reqData),
      success: function (responseData) {
        if (responseData.error == false) {
          if (responseData.restartRequired) {
            $("#rebootRequired").show();
          }
        } else {
          $("#jsErrorText").text(
            "There was an error resetting the configuration."
          );
          $("#jsError").show();
          setTimeout(() => {
            $("#jsError").hide();
          }, 5000);
        }
      },
      error: function (error) {
        $("#jsErrorText").text(
          "There was an error resetting the configuration."
        );
        $("#jsError").show();
        setTimeout(() => {
          $("#jsError").hide();
        }, 5000);
      },
    });
  }
}

function clearSensorConfig() {
  if (
    confirm(
      "Are you sure you wish to reset all sensor configuration? This may also reallocate SNMP index numbers"
    )
  ) {
    let reqData = new Object();
    reqData.webAction = "reset-sensor-config";

    $.ajax({
      url: ajaxBase,
      contentType: "application/json",
      type: "POST",
      dataType: "json",
      timeout: 10000,
      data: JSON.stringify(reqData),
      success: function (responseData) {
        if (responseData.error == false) {
          if (responseData.restartRequired) {
            $("#rebootRequired").show();
          }
        } else {
          $("#jsErrorText").text(
            "There was an error resetting the sensor configuration."
          );
          $("#jsError").show();
          setTimeout(() => {
            $("#jsError").hide();
          }, 5000);
        }
      },
      error: function (error) {
        $("#jsErrorText").text(
          "There was an error resetting the sensor configuration."
        );
        $("#jsError").show();
        setTimeout(() => {
          $("#jsError").hide();
        }, 5000);
      },
    });
  }
}

getConfig();
showSection("info");
