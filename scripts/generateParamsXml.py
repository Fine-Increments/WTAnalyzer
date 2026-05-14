# Parameters list for the pwm.py script
params = [
    ['Choice', 'Method', [ 'Linear', 'Reciprocal', 'Exponential' ], 2], # Choice: Linear/Reciprocal/Exponential (default Exponential)
    ['Int', '#Frames', 20, 100, 50],                                    # Integer: range 20 to 100, default 50
]

# Examples for Float and Bool parameters:
#   ['Bool', 'Check', True]
#   ['Float', 'X', 0.0, 50.0, 10.0]

# You don't have to change this code at all
print('<?xml version="1.0" encoding="UTF-8"?>\n')
print('<ParameterSet>')
for param in params:
    paramType = param[0]
    if paramType == 'Float':
        print('  <Float name="' + param[1] + '" minVal="' + str(param[2]) + '" maxVal="' + str(param[3]) + '" defaultVal="' + str(param[4]) + '"/>')
    if paramType == 'Int':
        print('  <Int name="' + param[1] + '" minVal="' + str(param[2]) + '" maxVal="' + str(param[3]) + '" defaultVal="' + str(param[4]) + '"/>')
    elif paramType == 'Bool':
        print('  <Bool name="' + param[1] + '" minVal="0" maxVal="1" defaultVal="' + ('1' if param[2] else '0') + '"/>')
    elif paramType == 'Choice':
        print('  <Choice name="' + param[1] + '" minVal="0" maxVal="' + str(len(param[2]) - 1) + '" defaultVal="' + str(param[3]) + '">')
        for item in param[2]:
            print('   <Item String="' + item + '"/>')
        print('  </Choice>')
print('</ParameterSet>')
