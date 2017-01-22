import Bridge from './Bridge.js';
const bridge = new Bridge();

let updateCb = null;
let setBoundsCb = null;
let showCb = null;
let hideCb = null;
bridge.onReceive(function(command, object) {
	if (command === "update" && updateCb) {
		updateCb(object);
	} else if (command == "setBounds" && setBoundsCb) {
		setBoundsCb(
			object.getChild('x').getValue().getInt(),
			object.getChild('y').getValue().getInt(),
			object.getChild('width').getValue().getInt(),
			object.getChild('height').getValue().getInt()
		);
	} else if (command == "show" && showCb) {
		showCb();
	} else if (command == "hide" && hideCb) {
		hideCb();
	}
});

export default {
	isSupported: function() {
		return bridge.isSupported();
	},
	connectTo: function(port) {
		return bridge.connectTo(port);
	},
	onUpdateReceived: function(cb) {
		updateCb = cb;
	},
	onSetBoundsReceived: function(cb) {
		setBoundsCb = cb;
	},
	onShowReceived: function(cb) {
		showCb = cb;
	},
	onHideReceived: function(cb) {
		hideCb = cb;
	},
	send: function(object) {
		return bridge.send("update", object);
	},
	requestUpdate: function() {
		return bridge.send("requestUpdate");
	}
};
