/* Keep browser-owned Emscripten callbacks out of Wasm while an Asyncify call is unresolved. */
mergeInto(LibraryManager.library, {
	$SurrealNativeCallGate__deps: ["$JSEvents", "$safeSetTimeout", "$callUserCallback"],
	$SurrealNativeCallGate: {
		installed: false,
		flushScheduled: false,
		queueLimit: 512,
		queue: [],
		nextTimerId: 1,
		timers: {},
		diagnostics: { deferred: 0, invoked: 0, dropped: 0, queued: 0, highWater: 0, overflowed: false, overflows: 0 },
		blocked: function () { return globalThis.surrealXRNativeCallsBlocked === true; },
		expose: function () {
			this.diagnostics.queued = this.queue.length;
			globalThis.SurrealNativeCallGateDiagnostics = this.diagnostics;
		},
		isCoalescible: function (type) {
			return type === "mousemove" || type === "touchmove" || type === "resize" || type === "scroll";
		},
		snapshotEvent: function (event) {
			var snapshot = {};
			var properties = ["type", "timeStamp", "key", "code", "char", "location", "repeat", "locale",
				"charCode", "keyCode", "which", "ctrlKey", "shiftKey", "altKey", "metaKey", "screenX", "screenY",
				"clientX", "clientY", "pageX", "pageY", "movementX", "movementY", "button", "buttons", "deltaX",
				"deltaY", "deltaZ", "deltaMode", "detail", "target", "relatedTarget"];
			for (var index = 0; index < properties.length; ++index) {
				try { snapshot[properties[index]] = event[properties[index]]; } catch (_) {}
			}
			function copyTouch(touch) {
				return { identifier: touch.identifier, screenX: touch.screenX, screenY: touch.screenY,
					clientX: touch.clientX, clientY: touch.clientY, pageX: touch.pageX, pageY: touch.pageY,
					isChanged: touch.isChanged, onTarget: touch.onTarget, target: touch.target };
			}
			["touches", "targetTouches", "changedTouches"].forEach(function (name) {
				if (event[name]) snapshot[name] = Array.from(event[name], copyTouch);
			});
			if (event.gamepad) {
				snapshot.gamepad = { id: event.gamepad.id, index: event.gamepad.index, connected: event.gamepad.connected,
					timestamp: event.gamepad.timestamp, mapping: event.gamepad.mapping, axes: Array.from(event.gamepad.axes || []),
					buttons: Array.from(event.gamepad.buttons || [], function (button) {
						return { value: button.value, pressed: button.pressed, touched: button.touched };
					}) };
			}
			snapshot.preventDefault = function () {};
			snapshot.stopPropagation = function () {};
			return snapshot;
		},
		defer: function (callback, event, type, owner, timerId) {
			if (this.diagnostics.overflowed) return 0;
			if (this.isCoalescible(type)) {
				for (var index = this.queue.length - 1; index >= 0; --index) {
					if (this.queue[index].type === type && this.queue[index].owner === owner) {
						this.queue.splice(index, 1);
						break;
					}
				}
			}

			if (this.queue.length >= this.queueLimit) {
				var discard = this.queue.findIndex(function (entry) {
					return SurrealNativeCallGate.isCoalescible(entry.type);
				});
				if (discard >= 0) {
					this.queue.splice(discard, 1);
					this.diagnostics.dropped++;
				} else if (this.isCoalescible(type)) {
					this.diagnostics.dropped++;
					this.expose();
					return 0;
				} else {
					this.diagnostics.overflowed = true;
					this.diagnostics.overflows++;
					this.expose();
					try { globalThis.dispatchEvent(new Event("surrealnativecallgateoverflow")); } catch (_) {}
					return 0;
				}
			}

			this.queue.push({ callback: callback, event: event, type: type, owner: owner, timerId: timerId });
			this.diagnostics.deferred++;
			this.diagnostics.highWater = Math.max(this.diagnostics.highWater, this.queue.length);
			if (event && event.cancelable && typeof event.preventDefault === "function") event.preventDefault();
			this.expose();
			return 0;
		},
		invokeOrDefer: function (callback, event, type, owner) {
			if (this.blocked()) return this.defer(callback, event, type, owner);
			this.diagnostics.invoked++;
			this.expose();
			return callback(event);
		},
		flush: function () {
			this.flushScheduled = false;
			while (!this.blocked() && this.queue.length) {
				var entry = this.queue.shift();
				if (entry.owner && JSEvents.eventHandlers.indexOf(entry.owner) < 0) continue;
				this.diagnostics.invoked++;
				this.expose();
				entry.callback(entry.event);
			}
			this.expose();
		},
		scheduleFlush: function () {
			if (this.blocked() || this.flushScheduled) return;
			if (this.diagnostics.overflowed) {
				for (var index = 0; index < this.queue.length; ++index) {
					if (this.queue[index].timerId) delete this.timers[this.queue[index].timerId];
				}
				this.queue = [];
				this.diagnostics.overflowed = false;
				this.expose();
				return;
			}
			if (!this.queue.length) return;
			this.flushScheduled = true;
			var gate = this;
			Promise.resolve().then(function () { gate.flush(); });
		},
		install: function () {
			if (this.installed) return;
			this.installed = true;
			if (Number.isInteger(globalThis.surrealNativeCallGateQueueLimit))
				this.queueLimit = Math.max(1, globalThis.surrealNativeCallGateQueueLimit);
			var originalRegister = JSEvents.registerOrRemoveHandler;
			JSEvents.registerOrRemoveHandler = function (eventHandler) {
				if (eventHandler.callbackfunc && !eventHandler.surrealNativeCallGateWrapped) {
					var originalHandler = eventHandler.handlerFunc;
					var eventType = eventHandler.eventTypeString;
					eventHandler.handlerFunc = function (event) {
						if (SurrealNativeCallGate.blocked()) {
							if (eventType === "beforeunload") return 0;
							return SurrealNativeCallGate.defer(originalHandler,
								SurrealNativeCallGate.snapshotEvent(event), eventType, eventHandler);
						}
						return SurrealNativeCallGate.invokeOrDefer(originalHandler, event, eventType, eventHandler);
					};
					eventHandler.surrealNativeCallGateWrapped = true;
				}
				return originalRegister(eventHandler);
			};
			globalThis.addEventListener("surrealnativecallgatechange", function () {
				SurrealNativeCallGate.scheduleFlush();
			});
			this.expose();
		},
		setTimeout: function (callback, milliseconds, userData) {
			var gate = this;
			var id = this.nextTimerId++;
			var rawId = safeSetTimeout(function () {
				var timer = gate.timers[id];
				if (!timer) return;
				timer.rawId = 0;
				if (gate.blocked()) gate.defer(function () {
					if (!gate.timers[id]) return;
					delete gate.timers[id];
					callUserCallback(function () { dynCall_vi(callback, userData); });
				}, null, "timer", null, id);
				else {
					delete gate.timers[id];
					dynCall_vi(callback, userData);
				}
			}, milliseconds);
			this.timers[id] = { rawId: rawId };
			return id;
		},
		clearTimeout: function (id) {
			var timer = this.timers[id];
			if (!timer) return;
			if (timer.rawId) clearTimeout(timer.rawId);
			delete this.timers[id];
		}
	},
	surreal_install_native_call_gate_js__deps: ["$SurrealNativeCallGate"],
	surreal_install_native_call_gate_js: function () { SurrealNativeCallGate.install(); },

	/* SDL implements SDL_AddTimer with this Emscripten system import. */
	emscripten_set_timeout__deps: ["$safeSetTimeout", "$SurrealNativeCallGate"],
	emscripten_set_timeout: function (callback, milliseconds, userData) {
		return SurrealNativeCallGate.setTimeout(callback, milliseconds, userData);
	},
	emscripten_clear_timeout__deps: ["$SurrealNativeCallGate"],
	emscripten_clear_timeout: function (id) { SurrealNativeCallGate.clearTimeout(id); }
});
