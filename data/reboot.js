let ajaxBase = "/json-api";
let reqData = new Object();
reqData.webAction = "reboot";

setTimeout(rebootNow, 3000);

function rebootNow() {
    $.ajax({
        url: ajaxBase,
        contentType: "application/json",
        type: "POST",
        dataType: "json",
        timeout: 500,
        data: JSON.stringify(reqData),
        success: function (responseData) {
            window.location.replace("/");
        },
        error: function (error) {
            setTimeout(checkPage, 5000);
        },
    });
}

function checkPage() {
    let statusData = new Object();
    statusData.webAction = "status";
    $.ajax({
        url: ajaxBase,
        contentType: "application/json",
        type: "POST",
        dataType: "json",
        timeout: 1000,
        data: JSON.stringify(statusData),
        success: function (responseData) {
            window.location.replace("/");
        },
        error: function (error) {
            setTimeout(checkPage, 5000);
        },
    });
}
