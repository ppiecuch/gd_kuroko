# repl_console.gd — In-game debug console using Kuroko
#
# Attach to a Control node with a LineEdit (input) and Label (output).
# Provides a live REPL for Kuroko scripting at runtime.

extends Control

export var max_history: int = 100

var history: PoolStringArray = PoolStringArray()
var history_index: int = -1

onready var output_label: Label = $OutputLabel
onready var input_field: LineEdit = $InputField

func _ready():
	input_field.connect("text_entered", self, "_on_input_submitted")
	_print_output("Kuroko v1.4.0 REPL — type expressions to evaluate")
	_print_output(">>> ")

func _on_input_submitted(text: String):
	if text.strip_edges().empty():
		return

	# Show input in output
	_print_output(">>> " + text)

	# Execute via Kuroko singleton
	var result = Kuroko.execute(text)
	if result != null:
		_print_output(str(result))

	# History
	history.append(text)
	if history.size() > max_history:
		history.remove(0)
	history_index = -1

	input_field.clear()

func _print_output(text: String):
	output_label.text += text + "\n"
	# Auto-scroll to bottom
	output_label.get_parent().ensure_control_visible(output_label)

func _input(event):
	if event is InputEventKey and event.pressed:
		if event.scancode == KEY_UP and history.size() > 0:
			# Navigate history up
			if history_index == -1:
				history_index = history.size() - 1
			elif history_index > 0:
				history_index -= 1
			input_field.text = history[history_index]
			input_field.caret_position = input_field.text.length()
		elif event.scancode == KEY_DOWN and history_index >= 0:
			# Navigate history down
			history_index += 1
			if history_index >= history.size():
				history_index = -1
				input_field.text = ""
			else:
				input_field.text = history[history_index]
				input_field.caret_position = input_field.text.length()
