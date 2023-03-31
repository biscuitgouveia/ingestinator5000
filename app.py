from subprocess import call

input_file = input("Enter the name of the input file: ")
output_file = input("What would you like to call the output file?")
output_file += ".mxf"

call(["./ingestinator", input_file, output_file])