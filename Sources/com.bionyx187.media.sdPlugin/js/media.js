// this is our global websocket, used to communicate from/to Stream Deck software
        // and some info about our plugin, as sent by Stream Deck software
        var websocket = null,
        uuid = null,
        actionInfo = {};

        function connectElgatoStreamDeckSocket(inPort, inUUID, inRegisterEvent, inInfo, inActionInfo) {
	    console.log("connectElgatoStreamDeckSocket()");
            uuid = inUUID;
            // please note: the incoming arguments are of type STRING, so
            // in case of the inActionInfo, we must parse it into JSON first
            actionInfo = JSON.parse(inActionInfo); // cache the info
            websocket = new WebSocket('ws://localhost:' + inPort);

            // if connection was established, the websocket sends
            // an 'onopen' event, where we need to register our PI
            websocket.onopen = function () {
		console.log("websocket.onopen()");
                var json = {
                    event:  inRegisterEvent,
                    uuid:   inUUID
                };
                // register property inspector to Stream Deck
		applySettings(actionInfo);
                websocket.send(JSON.stringify(json));
            }

           
			
			websocket.onmessage = function (evt) {
                
                var jsonObj = JSON.parse(evt.data);

                if (jsonObj.event == "sendToPropertyInspector")
                {
                    var payload = jsonObj.payload;

                    var profilesList = payload.Profiles;
    
                    var select = document.getElementById("profileList");
    
                    select.options.length=0;
    
                    for (var i = 0; i < profilesList.length; i++) 
                    {
                        var option = document.createElement("option");
                        option.innerHTML = profilesList[i];
                        option.value = profilesList[i];
    
                        select.options.add(option);
                    }

                    getSettings();
                }

                if (jsonObj.event == "didReceiveSettings")
                {
			applySettings(jsonObj);
                }            
            };
			
        }

	function applySettings(jsonObj)
	{
	    console.log("applySettings");
            var settings = jsonObj.payload.settings;
	    console.log("settings: " + JSON.stringify(settings));
		var refreshTime = document.getElementById("refresh_time");

            if (settings.hasOwnProperty("refresh_time"))
            {
                refreshTime.value = settings.refresh_time;
            }
	}

        function getSettings()
        {
            if (websocket)
            {
                const json = 
                {    
                    "event": "getSettings",
                    "context": uuid         
                };
                websocket.send(JSON.stringify(json));
            }
           
        }

        function setSettings(value, param)
        {
	    console.log("in setSettings");
            if (websocket)
            {
		console.log("have websocket");
		var refreshTime = document.getElementById("refresh_time");
                const json = 
                {
                    "event": "setSettings",
                    "context": uuid,
                    "payload":{
                        "refresh_time" : parseInt(refreshTime.value, 10),
                    }
                };
                websocket.send(JSON.stringify(json));
		console.log("settings sent: " + JSON.stringify(json));
            }
        }

        // our method to pass values to the plugin
        function sendValueToPlugin(value, param) {
            if (websocket) {
                const json = {
                        "action": actionInfo['action'],
                        "event": "sendToPlugin",
                        "context": uuid,
                        "payload": {
                            [param] : value
                        }
                 };
                 websocket.send(JSON.stringify(json));
            }
        }
