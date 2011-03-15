"use strict";

function printf(str)
{
	if(typeof(console) == "undefined")
	{
		postMessage({type:EV_PRINT, 'str':str});
	}
	else
	{
		console.log(str);
	}
}

function sendProtoBuf_async(pbuf, oncomplete, onerror, timeout_val, async)
{
	if(typeof(timeout_val) == "undefined")
		timeout_val = 5000;

	if(typeof(async) == "undefined")
		async = true;

	//Encode protocol buffer
	var data = new PROTO.ByteArrayStream;
	pbuf.SerializeToStream(data);
	
	//Encode as binary
	var arr = new Uint8Array(data.array_),
		bb = new BlobBuilder,
		XHR = new XMLHttpRequest,
		timeout = setTimeout(function() { XHR.abort(); }, timeout_val);
		
	bb.append(arr.buffer);
	
	//Build request
	XHR.open("POST", "/", async);
	XHR.onreadystatechange = function()
	{
		if(XHR.readyState == 4)
		{
			clearTimeout(timeout);
		
			if(XHR.status == 200 && XHR.responseText.length > 1)
			{
				var str = XHR.responseText,
					arr = new Array(str.length - 1),
					i;
					
				for(i=1; i<str.length; i++)
				{
					arr[i-1] = str.charCodeAt(i) & 0xff;
				}

				var msg = new Network.ServerPacket;
				msg.ParseFromStream(new PROTO.ByteArrayStream(arr));
				oncomplete(msg);
			}
			else
			{
				//Call error handler
				onerror();
			}
		}
	}
	
	//Encode blob
	XHR.send(bb.getBlob("application/octet-stream"));
}


function sendProtoBuf_sync(pbuf, timeout_val)
{
	var result = null;

	sendProtoBuf_async(pbuf, 
		function(msg) { result = msg; },
		function() { },
		timeout_val,
		false);
		
	return result;
}


//Turns a protocol buffer into a utf-8 stream.  only uses lower 7 bits.
//   AAAAHH  Why you are you stupid websockets?!? >:P
function pbuf_to_raw(pbuf)
{
	//Serialize protocol buffer to array
	var stream = new PROTO.ByteArrayStream;
	pbuf.SerializeToStream(stream);	
	var arr = stream.array_,
		raw = "",
		len = arr.length,
		w = 0, i = 0, j = 0;
			
	for(j=0; j<7; ++j)
		arr.push(0);
	
	while(i < len)
	{
		//Read 4
		w = 0;
		for(j=0; j<4; ++j)
		{
			w += arr[i++] << (j*8);
		}
		
		//Copy 4
		for(j=0; j<4; ++j)
		{
			raw += String.fromCharCode(w & 0x7f);
			w >>= 7;
		}
		
		//Read 3
		for(j=0; j<3; ++j)
		{
			w += arr[i++] << (j*8 + 4);
		}
		
		//Copy 4
		for(j=0; j<4; ++j)
		{
			raw += String.fromCharCode(w & 0x7f);
			w >>= 7;
		}
	}
	
	return raw.substr(0, Math.ceil(Math.floor(len * 8) / 7));
}

function raw_to_pbuf(raw)
{
	var len = raw.length,
		w, i, j, k,
		arr = new Array(0, Math.ceil(Math.floor(len * 7) / 8) + 7);

	//Pad string
	for(i=0; i<8; ++i)
		raw += String.fromCharCode(0);

	i = 0;
	k = 0;
	while(i < len)
	{
		//Read 4
		w = 0;
		for(j=0; j<4; ++j)
		{
			w += (raw.charCodeAt(i++) & 0x7f) << (7*j);
		}
		
		//Copy 3
		for(j=0; j<3; ++j)
		{
			arr[k++] = w & 0xff;
			w >>= 8;
		}
		
		//Read 4
		for(j=0; j<4; ++j)
		{
			w += (raw.charCodeAt(i++) & 0x7f) << (7*j + 4);
		}
		
		//Copy 4
		for(j=0; j<4; ++j)
		{
			arr[k++] = w & 0xff;
			w >>= 8;
		}
	}
	
	var stream = new PROTO.ByteArrayStream(arr),
		pbuf = new Network.ServerPacket;
	pbuf.ParseFromStream(stream)
	return pbuf;
}


